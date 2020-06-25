#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/randr.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <cairo/cairo-xcb.h>
#include <pango/pangocairo.h>
#include <fontconfig/fontconfig.h>

#include <sys/types.h> 
#include <unistd.h>

const int32_t MIN_WIDTH = 20;
const int32_t MIN_HEIGHT = 20;

// Mask 1 = Alt key
// Mask 4 = Super key
#define PRIMARY_MOD_KEY XCB_MOD_MASK_4

#define CONFIG_BAR_BORDER	0
#define CONFIG_COLOR_BAR	0xFFFFFF
#define CONFIG_COLOR_BAR_BORDER	0xFFFFFF
#define CONFIG_COLOR_BAR_TEXT	0x000000
#define CONFIG_BORDER_WIDTH	3
#define CONFIG_FONT		"Monospace 10"
#define CONFIG_BAR_HEIGHT	18
#define CONFIG_FRAME_BAR	18

#define CONFIG_COLOR_FRAME_BACK_FOCUS	0x666699
#define CONFIG_COLOR_FRAME_BACK_UNFOCUS	0x888888
#define CONFIG_COLOR_FRAME_BORDER_FOCUS 	0xFF9933
#define CONFIG_COLOR_FRAME_BORDER_UNFOCUS	0x777777

#define WM_MAX_WINDOWS 64
#define WM_MAX_MONITORS 16

enum {
	WM_ATOMS_PROTOCOLS, 
	WM_ATOMS_DELETE,
	WM_ATOMS_STATE,
	WM_ATOMS_TAKEFOCUS,

	WM_ATOMS_ALL
};

typedef struct {
	xcb_window_t	frame;
	xcb_window_t 	id;
	char		name[64];
	bool		visible;
} wm_window_t;

typedef struct {
	xcb_rectangle_t rect;
} wm_monitor_t;

static xcb_connection_t *connection = NULL;
static xcb_drawable_t 	root;
static xcb_key_symbols_t *syms;
static xcb_atom_t 	wm_atoms[WM_ATOMS_ALL];
static wm_window_t 	windows[WM_MAX_WINDOWS] = { 0 };
static uint32_t		windows_len = 0;
static xcb_screen_t 	*screen;

static wm_window_t	current = { 0 };

static xcb_window_t	overview;

// Bar
static xcb_window_t	bar;
static bool		bar_visible = true;
static const uint32_t	bar_height = CONFIG_BAR_HEIGHT;
static xcb_gcontext_t 	bar_gc;

static uint32_t 		values[3];
static xcb_get_geometry_reply_t	*geom;
static bool			running = false;

static wm_monitor_t	monitors[WM_MAX_MONITORS] = { 0 };
static uint32_t		monitors_len = 0;

static xcb_visualtype_t	*visual_type = NULL;

static cairo_surface_t 	*cr_surface = NULL;
static cairo_t		*cr = NULL;
static PangoLayout 	*pa_layout = NULL;

void
text_render_setup(void)
{
	cr_surface = cairo_xcb_surface_create(connection,
			root, visual_type,
			screen->width_in_pixels,
			screen->height_in_pixels);

	cr = cairo_create(cr_surface);
	cairo_paint(cr);

	pa_layout = pango_cairo_create_layout(cr);
	PangoFontDescription *pa_desc = pango_font_description_from_string(CONFIG_FONT);
	pango_layout_set_font_description(pa_layout, pa_desc);
	pango_font_description_free(pa_desc);
	pa_desc = NULL;
}

void
text_render_draw(const xcb_window_t window,
		const char *text,
		const double x,
		const double y,
		const double r,
		const double g,
		const double b,
		const double a)
{
	static int32_t pa_height = 0;
	xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(
			connection,
			window);

	xcb_get_geometry_reply_t *geom_reply = xcb_get_geometry_reply(
			connection,
			geom_cookie,
			NULL);

	if (!geom_reply)
	{
		fprintf(stderr, "ERROR: Cannot get geometry of window"
				" %d.\n", window);
		return;
	}

	cairo_surface_flush(cr_surface);
	cairo_xcb_surface_set_drawable(cr_surface,
			window,
			geom_reply->width,
			geom_reply->height);

	free(geom_reply);

	pango_layout_set_text(pa_layout, text, -1);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x, y);
	pango_cairo_update_layout(cr, pa_layout);
	pango_layout_get_size(pa_layout, NULL, &pa_height);
	pango_cairo_show_layout(cr, pa_layout);

	cairo_surface_flush(cr_surface);
}

void
text_render_destroy(void)
{
	g_object_unref(pa_layout);
	pa_layout = NULL;
	cairo_destroy(cr);
	cr = NULL;
	cairo_surface_destroy(cr_surface);
	cr_surface = NULL;
}

void
spawn(const char *cmd)
{
	if (fork()) return;
	setsid();
	execvp(cmd, (char *[2]) { (char *) cmd, NULL });
}

void
send_event(const xcb_window_t window, const xcb_atom_t proto)
{
	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = window,
		.type = wm_atoms[WM_ATOMS_PROTOCOLS],
		.data.data32 = {
			proto,
			XCB_CURRENT_TIME
		}
	};

	xcb_send_event(connection, false, window, XCB_EVENT_MASK_NO_EVENT,
			(char *) &event);
}

void
set_border(const xcb_window_t window, const bool focus)
{
#if 0
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_BORDER_WIDTH,
			(uint32_t [1]) { CONFIG_BORDER_WIDTH });
#endif

	uint32_t color = (focus) ? CONFIG_COLOR_FRAME_BORDER_FOCUS : CONFIG_COLOR_FRAME_BORDER_UNFOCUS;

	xcb_change_window_attributes(connection,
			window,
			XCB_CW_BORDER_PIXEL,
			(uint32_t [1]) { color });
}

void
setup_key(const xcb_keysym_t keysym)
{
	xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(syms, keysym);

	xcb_grab_key(connection, 1, root,
			PRIMARY_MOD_KEY, keycode[0],
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	free(keycode);
}

void
set_focus(const xcb_window_t window)
{
	xcb_set_input_focus(connection,
			XCB_INPUT_FOCUS_PARENT,
			window,
			XCB_CURRENT_TIME);
}

char *
get_name(const xcb_window_t window)
{
	static char name[64] = { 0 };
	xcb_get_property_cookie_t cookie = xcb_get_property(connection,
			0, window,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
			0, 64);

	xcb_get_property_reply_t *reply = xcb_get_property_reply(
			connection, cookie, NULL);

	if (reply)
	{
		int32_t len = xcb_get_property_value_length(reply);
		memset(name, '\0', 64);

		if (len != 0)
		{
			snprintf(name, 64, "%.*s",
					len,
					(char *) xcb_get_property_value(reply));
		}

		free(reply);

		return name;
	}

	return NULL;
}

int32_t
find_window(const xcb_window_t window_id)
{
	for (uint32_t i = 0; i < windows_len; ++i)
	{
		if (windows[i].id == window_id)
		{
			return i;
		}
	}

	return -1;
}

void
setup_overview(void)
{
	overview = xcb_generate_id(connection);
	xcb_create_window(connection,
			XCB_COPY_FROM_PARENT,
			overview,
			root,
			0, 0,
			150, 450,
			3,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			screen->root_visual,
			0, NULL);
}

void
setup_bar(void)
{
	bar = xcb_generate_id(connection);
	xcb_create_window(connection,
			XCB_COPY_FROM_PARENT,
			bar,
			root,
			0, 0,
			monitors[0].rect.width, bar_height,
			CONFIG_BAR_BORDER,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			screen->root_visual,
			XCB_CW_BACK_PIXEL |
				XCB_CW_BORDER_PIXEL |
				XCB_CW_OVERRIDE_REDIRECT |
				XCB_CW_EVENT_MASK,
			(uint32_t []) {
				CONFIG_COLOR_BAR,
				CONFIG_COLOR_BAR_BORDER,
				true,
				XCB_EVENT_MASK_BUTTON_PRESS |
					XCB_EVENT_MASK_EXPOSURE
			});

	bar_gc = xcb_generate_id(connection);
	xcb_create_gc(connection, bar_gc, bar,
			XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
			(uint32_t [2]) {
				[0] = CONFIG_COLOR_BAR,
				[1] = 0
			});

	xcb_map_window(connection, bar);
}

void
update_bar(void)
{
	// Clear with rect
	xcb_poly_fill_rectangle(connection,
			bar, bar_gc,
			1, (xcb_rectangle_t [1]) { {
				.x = 0,
				.y = 0,
				.width = monitors[0].rect.width,
				.height = bar_height
			} });

	int32_t index = find_window(current.id);
	if (index == -1)
	{
		return;
	}

	//printf("text_render_draw: %s\n", windows[index].name);
	text_render_draw(bar, windows[index].name, 5, 0, 
			0.0, 0.0, 0.0, 1.0);
}

void
toggle_bar(void)
{
	xcb_void_cookie_t (*xcb_toggle_window)(xcb_connection_t *, xcb_window_t) = xcb_unmap_window;

	bar_visible = !bar_visible;

	if (bar_visible)
	{
		xcb_toggle_window = xcb_map_window;
	}

	xcb_toggle_window(connection, bar);
	update_bar();
}

xcb_cursor_t
cursor_get(const uint32_t cursor_id)
{
	xcb_font_t cursor_font = xcb_generate_id(connection);
	xcb_open_font(connection, cursor_font, 6, "cursor");

	xcb_cursor_t cursor = xcb_generate_id(connection);
	xcb_create_glyph_cursor(connection,
			cursor,
			cursor_font,
			cursor_font,
			cursor_id,
			cursor_id + 1,
			0x3232, 0x3232, 0x3232, 0xeeee, 0xeeee, 0xeeec);

	return cursor;
}

void
cursor_set(const xcb_cursor_t cursor, const xcb_window_t window)
{
	xcb_change_window_attributes(connection, window,
			XCB_CW_CURSOR,
			(uint32_t []) { cursor });
}

void
cursor_free(xcb_cursor_t cursor)
{
	xcb_free_cursor(connection, cursor);
}

void
cursor_change(const xcb_window_t window, const uint32_t cursor_id)
{
	xcb_cursor_t cursor = cursor_get(cursor_id);
	cursor_set(cursor, window);
	cursor_free(cursor);
}

void
update_window_title(const xcb_window_t window)
{
	int32_t index = find_window(window);
	if (index == -1)
	{
		return;
	}

	strcpy(windows[index].name, get_name(window));
}

xcb_window_t
frame_find_child(const xcb_window_t frame)
{
	for (uint32_t i = 0; i < windows_len; ++i)
	{
		if (windows[i].frame == frame)
		{
			return windows[i].id;
		}
	}

	return 0;
}

xcb_window_t
child_find_frame(const xcb_window_t child)
{
	for (uint32_t i = 0; i < windows_len; ++i)
	{
		if (windows[i].id == child)
		{
			return windows[i].frame;
		}
	}

	return 0;
}

void
update_current(const xcb_window_t frame)
{
	const xcb_window_t child = frame_find_child(frame);
	if (child == 0)
	{
		return;
	}

	set_border(current.frame, false);

	current.frame = frame;
	current.id = child;

	set_border(current.frame, true);
	set_focus(current.frame);
}

void
frame_update_size(const xcb_window_t frame,
		const uint32_t width,
		const uint32_t height)
{
	const xcb_window_t child_win = frame_find_child(frame);
	if (child_win == 0)
	{
		return;
	}

	xcb_configure_window(connection,
			frame,
			XCB_CONFIG_WINDOW_WIDTH |
			XCB_CONFIG_WINDOW_HEIGHT,
			(uint32_t []) {
				width, height
			});

	xcb_configure_window(connection,
			child_win,
			XCB_CONFIG_WINDOW_WIDTH |
			XCB_CONFIG_WINDOW_HEIGHT,
			(uint32_t []) {
				width, height - CONFIG_FRAME_BAR
			});

	// Clear old render
	xcb_gcontext_t frame_gc = xcb_generate_id(connection);

	xcb_create_gc(connection, frame_gc, frame,
			XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
			(uint32_t [2]) {
				[0] = CONFIG_COLOR_FRAME_BACK_FOCUS,
				[1] = 0
			});

	xcb_poly_fill_rectangle(connection,
			frame, frame_gc,
			1, (xcb_rectangle_t [1]) { {
				.x = 0,
				.y = 0,
				.width = width,
				.height = height
			} });

	xcb_free_gc(connection, frame_gc);
}

void
new_window(xcb_generic_event_t *event)
{
	xcb_map_request_event_t *e = (xcb_map_request_event_t *) event;

	printf("Map request\n");

	xcb_window_t frame = xcb_generate_id(connection);
	xcb_get_geometry_reply_t *win_geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, e->window), NULL);

	xcb_create_window(connection,
			0,
			frame,
			root,
			0, 0,
			win_geom->width, win_geom->height + CONFIG_FRAME_BAR,
			2,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			screen->root_visual,
				XCB_CW_BACK_PIXEL |
				XCB_CW_BORDER_PIXEL |
				XCB_CW_OVERRIDE_REDIRECT |
				XCB_CW_EVENT_MASK,
			(uint32_t []) {
				CONFIG_COLOR_FRAME_BACK_FOCUS,
				CONFIG_COLOR_FRAME_BORDER_FOCUS,
				true,
				XCB_EVENT_MASK_BUTTON_PRESS |
					XCB_EVENT_MASK_EXPOSURE |
					XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT}
			);

	xcb_change_window_attributes_checked(connection,
			e->window,
			XCB_CW_EVENT_MASK,
			(uint32_t [1]) {XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT});

	xcb_reparent_window(connection, e->window, frame, 0, CONFIG_FRAME_BAR);

	printf("New window for: %d | frame: %d\n", e->window, frame);

	// Add to list
	windows[windows_len].id = e->window;
	windows[windows_len].frame = frame;
	strcpy(windows[windows_len].name, get_name(e->window));
	windows[windows_len].visible = true;

	++windows_len;

	update_current(frame);
	update_bar();

	printf("Mapping window: %s\n", get_name(e->window));

	xcb_map_window(connection, e->window);
	xcb_map_window(connection, frame);
	xcb_flush(connection);
}

void
property_notify(xcb_generic_event_t *event)
{
	xcb_property_notify_event_t *e = (xcb_property_notify_event_t *) event;

	//printf("Atom: %d == %d\n", e->atom, XCB_ATOM_WM_NAME);

	if (e->atom == XCB_ATOM_WM_NAME)
	{	// Window name changed
		update_window_title(e->window);
		update_bar();
	}

	xcb_flush(connection);
}

void
frame_kill(const xcb_window_t frame)
{
	send_event(frame_find_child(frame), wm_atoms[WM_ATOMS_DELETE]);
	xcb_unmap_window(connection, frame);

	current.id = root;
	current.frame = root;
	memset(current.name, '\0', sizeof(current.name));
	current.visible = false;
}

void
key_press(xcb_generic_event_t *event)
{
	xcb_key_press_event_t *e = (xcb_key_press_event_t *) event;

	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(syms, e->detail, 0);

	//printf("keysym got: %d | q: %d\n", keysym, XK_q);
	switch (keysym)
	{
	case XK_e:	// Exit window manager
		//printf("state: %d\n", e->state);
		if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
		{
			printf("Closing wm\n");
			running = false;
		}
		break;
	case XK_q:	// Kill window
		if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
		{
			frame_kill(e->child);
			update_bar();
			//xcb_kill_client(connection, window);
		}
		break;
	case XK_a:	// Raise window
		update_current(e->child);
		values[0] = XCB_STACK_MODE_ABOVE;
		xcb_configure_window(connection,
				e->child,
				XCB_CONFIG_WINDOW_STACK_MODE,
				values);
		update_bar();
		break;
	case XK_d:	// dmenu
		spawn("dmenu_run");
		break;
	case XK_b:
		toggle_bar();
		break;
	}

	xcb_flush(connection);
}

void
button_press(xcb_generic_event_t *event)
{
	xcb_button_press_event_t *e = (xcb_button_press_event_t *) event;

	// Ignore it (background)
	if (e->child == 0)
	{
		return;
	}

	// Set border of old one as un-focused
	const xcb_window_t window = e->child;
	update_current(window);

	// Raise window
	values[0] = XCB_STACK_MODE_ABOVE;
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_STACK_MODE,
			values);

	update_bar();

	geom = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, window),
			NULL);

	switch (e->detail)
	{
	case 1: // Move
		values[2] = 1;
		xcb_warp_pointer(connection, XCB_NONE, window, 0, 0, 0, 0, 1, 1);
		cursor_change(window, XC_fleur);
		break;
	case 3: // Resize
		values[2] = 3;
		xcb_warp_pointer(connection, XCB_NONE, window, 0, 0, 0, 0, geom->width, geom->height);
		cursor_change(window, XC_sizing);
		break;
	}

	free(geom);

	xcb_grab_pointer(connection, 0, root,
			XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			root, XCB_NONE, XCB_CURRENT_TIME);

	xcb_flush(connection);
}

void
enter_window(xcb_generic_event_t *event)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *) event;

	update_current(e->event);
	update_bar();

	xcb_flush(connection);
}

void
mouse_motion(xcb_generic_event_t *event)
{
	(void) event;

	xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(
			connection,
			xcb_query_pointer(connection, root),
			0);

	if (!pointer)
	{
		return;
	}

	const int16_t pointer_x = pointer->root_x;
	const int16_t pointer_y = pointer->root_y;

	//printf("motion: %d %d\n", pointer_x, pointer_y);

	switch (values[2])
	{
	case 1: // Move
	{
		geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, current.frame), NULL);

		values[0] = (pointer_x + geom->width > screen->width_in_pixels) ? (screen->width_in_pixels - geom->width) : pointer_x;
		values[1] = (pointer_y + geom->height > screen->height_in_pixels) ? (screen->height_in_pixels - geom->height) : pointer_y;

		free(geom);

		//printf("Move value: %d %d\n", values[0], values[1]);
		xcb_configure_window(connection, current.frame, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
		xcb_flush(connection);
	} break;
	case 3: // Resize
	{
		geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, current.frame), NULL);

		if ((pointer_x - geom->x) < MIN_WIDTH || (pointer_y - geom->y) < MIN_HEIGHT)
		{
			break;
		}

		values[0] = pointer_x - geom->x;
		values[1] = pointer_y - geom->y;

		free(geom);

		frame_update_size(current.frame, values[0], values[1]);
		//xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
		xcb_flush(connection);
	} break;
	}

	free(pointer);
}

void
button_release(xcb_generic_event_t *event)
{
	(void) event;

	xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
	xcb_flush(connection);
}

void
setup_randr(void)
{
	/*
	 * Setup randr by querying its version
	 */

	xcb_randr_query_version_cookie_t version_cookie = xcb_randr_query_version(
			connection,
			XCB_RANDR_MAJOR_VERSION,
			XCB_RANDR_MINOR_VERSION);

	xcb_randr_query_version_reply_t *version_reply = xcb_randr_query_version_reply(
			connection,
			version_cookie,
			NULL);

	if (version_reply)
	{
		free(version_reply);
	}

	/*
	 * Get screen resources
	 */

	xcb_randr_get_screen_resources_cookie_t screen_res_cookie = xcb_randr_get_screen_resources(
			connection,
			root);

	xcb_randr_get_screen_resources_reply_t *screen_res_reply = xcb_randr_get_screen_resources_reply(
			connection,
			screen_res_cookie,
			NULL);

	if (!screen_res_reply)
	{
		return;
	}

	/*
	 * CRTC
	 */

	const uint32_t crtcs_len = xcb_randr_get_screen_resources_crtcs_length(screen_res_reply);
	xcb_randr_crtc_t *crtcs = xcb_randr_get_screen_resources_crtcs(screen_res_reply);

	/*
	 * Send request to X server about crtc information
	 */
	xcb_randr_get_crtc_info_cookie_t crtcs_cookie[crtcs_len];

	for (uint32_t i = 0; i < crtcs_len; ++i)
	{
		crtcs_cookie[i] = xcb_randr_get_crtc_info(connection, crtcs[i], 0);
	}

	/*
	 * Recieve reply from X server about crtc information
	 */
	xcb_randr_get_crtc_info_reply_t *crtcs_reply[crtcs_len];

	for (uint32_t i = 0; i < crtcs_len; ++i)
	{
		crtcs_reply[i] = xcb_randr_get_crtc_info_reply(connection, crtcs_cookie[i], 0);

		if (!crtcs_reply[i])
		{
			continue;
		}

		if (crtcs_reply[i]->x != 0 || crtcs_reply[i]->y != 0 ||
				crtcs_reply[i]->width != 0 || crtcs_reply[i]->height != 0)
		{
			monitors[monitors_len].rect.x = crtcs_reply[i]->x;
			monitors[monitors_len].rect.y = crtcs_reply[i]->y;
			monitors[monitors_len].rect.width = crtcs_reply[i]->width;
			monitors[monitors_len].rect.height = crtcs_reply[i]->height;

			printf("Monitor: %d: (%d,%d) %d x %d\n",
					monitors_len,
					monitors[monitors_len].rect.x,
					monitors[monitors_len].rect.y,
					monitors[monitors_len].rect.width,
					monitors[monitors_len].rect.height);

			++monitors_len;
		}

		free(crtcs_reply[i]);
	}

	free(screen_res_reply);
}

void
setup_visual_type(void)
{
	for (xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
			depth_iter.rem;
			xcb_depth_next(&depth_iter))
	{
		for (xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
				visual_iter.rem;
				xcb_visualtype_next(&visual_iter))
		{
			if (screen->root_visual == visual_iter.data->visual_id)
			{
				visual_type = visual_iter.data;
				return;
			}
		}
	}
}

void
unmap_notify(xcb_generic_event_t *event)
{
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *) event;

#if 1
	xcb_window_t frame = child_find_frame(e->window);

	if (!frame)
	{
		return;
	}
#else
	xcb_window_t frame = current.frame;
	xcb_window_t window = current.id;

	current.id = 0;
	current.frame = 0;
#endif

	printf("unmap_notify: %d | frame: %d\n", e->window, frame);

	xcb_unmap_window(connection, frame);

	xcb_reparent_window(connection, e->window, root, 0, 0);

	xcb_destroy_window(connection, e->window);
}

void
cleanup(void)
{
	xcb_free_gc(connection, bar_gc);
	text_render_destroy();
	xcb_key_symbols_free(syms);
	xcb_set_input_focus(connection, XCB_NONE,
			XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_CURRENT_TIME);

	for (uint32_t i = 0; i < windows_len; ++i)
	{
		xcb_kill_client(connection, windows[i].id);

		xcb_unmap_window(connection, windows[i].frame);
		xcb_destroy_window(connection, windows[i].frame);
	}

	xcb_unmap_window(connection, bar);
	xcb_destroy_window(connection, bar);

	xcb_flush(connection);
	xcb_disconnect(connection);
	printf("Closing martwm\n");
}

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	printf("Running martwm\n");

	atexit(cleanup);
	connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(connection))
	{
		return 1;
	}

	screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	root = screen->root;

	xcb_intern_atom_cookie_t atom_cookies[WM_ATOMS_ALL];

	/* 
	 * Make requests for atoms
	 */
	atom_cookies[WM_ATOMS_PROTOCOLS] = 	xcb_intern_atom(connection, 0, 12, "WM_PROTOCOLS");
	atom_cookies[WM_ATOMS_DELETE] = 	xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
	atom_cookies[WM_ATOMS_STATE] = 		xcb_intern_atom(connection, 0, 8,  "WM_STATE");
	atom_cookies[WM_ATOMS_TAKEFOCUS] = 	xcb_intern_atom(connection, 0, 13, "WM_TAKE_FOCUS");

	/*
	 * Receive responses for atoms
	 */
	for (uint32_t i = 0; i < WM_ATOMS_ALL; ++i)
	{
		xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(
				connection,
				atom_cookies[i],
				NULL);

		if (atom_reply)
		{
			wm_atoms[i] = atom_reply->atom;
			free(atom_reply);
		}
	}

	setup_randr();

	/*
	 * Keycodes
	 */
	syms = xcb_key_symbols_alloc(connection);

	// Grab any key with the modifer used
	xcb_grab_key(connection, 1, root,
			PRIMARY_MOD_KEY, XCB_NO_SYMBOL,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	xcb_grab_button(connection, 0, root,
			XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			root, XCB_NONE, 1, PRIMARY_MOD_KEY);

	xcb_grab_button(connection, 0, root,
			XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			root, XCB_NONE, 3, PRIMARY_MOD_KEY);

	// Change root cursor
	cursor_change(root, XC_left_ptr);

	setup_overview();
	setup_bar();

	uint32_t root_values[1] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_PROPERTY_CHANGE
		| XCB_EVENT_MASK_BUTTON_PRESS
	};
	setup_visual_type();

	xcb_generic_error_t *error = xcb_request_check(connection,
			xcb_change_window_attributes_checked(connection, root,
				XCB_CW_EVENT_MASK, root_values));

	if (error)
	{
		fprintf(stderr, "ERROR Cannot set root window attributes\n");
		free(error);
		exit(1);
	}

	text_render_setup();

	xcb_flush(connection);

	xcb_generic_event_t 	*ev = NULL;
	void			(*events[XCB_NO_OPERATION])(xcb_generic_event_t *) = {
		[XCB_MAP_REQUEST] = new_window,
		[XCB_PROPERTY_NOTIFY] = property_notify,
		[XCB_KEY_PRESS] = key_press,
		[XCB_BUTTON_PRESS] = button_press,
		[XCB_ENTER_NOTIFY] = enter_window,
		[XCB_MOTION_NOTIFY] = mouse_motion,
		[XCB_BUTTON_RELEASE] = button_release,
		[XCB_UNMAP_NOTIFY] = unmap_notify

		//[XCB_DESTROY_NOTIFY] = ,
		//[XCB_CONFIGURE_REQUEST] = ,
		//[XCB_CONFIGURE_NOTIFY] = ,
		//[XCB_CLIENT_MESSAGE] = ,
	};

	running = true;

	while (running && (ev = xcb_wait_for_event(connection)))
	{
		if (events[ev->response_type & ~0x80] != NULL)
		{
			events[ev->response_type & ~0x80](ev);
		}

		free(ev);
	}

	exit(0);
}


