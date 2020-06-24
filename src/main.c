#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/randr.h>
#include <X11/keysym.h>

#include <sys/types.h> 
#include <unistd.h>

const int32_t MIN_WIDTH = 20;
const int32_t MIN_HEIGHT = 20;

// Mask 1 = Alt key
// Mask 4 = Super key
#define PRIMARY_MOD_KEY XCB_MOD_MASK_4

#define CONFIG_COLOR_FOCUS 	0xFF9933
#define CONFIG_COLOR_UNFOCUS	0x111111
#define CONFIG_BAR_BORDER	0
#define CONFIG_COLOR_BAR	0xFFFFFF
#define CONFIG_COLOR_BAR_BORDER	0xFFFFFF
#define CONFIG_COLOR_BAR_TEXT	0x000000
#define DEFAULT_FONT		"fixed"

#define WM_MAX_WINDOWS 64

enum {
	WM_ATOMS_PROTOCOLS, 
	WM_ATOMS_DELETE,
	WM_ATOMS_STATE,
	WM_ATOMS_TAKEFOCUS,

	WM_ATOMS_ALL
};

typedef struct {
	xcb_window_t 	id;
	char		name[64];
	bool		visible;
} wm_window_t;

static xcb_connection_t *connection = NULL;
static xcb_drawable_t 	root;
static xcb_key_symbols_t *syms;
static xcb_atom_t 	wm_atoms[WM_ATOMS_ALL];
static wm_window_t 	windows[WM_MAX_WINDOWS] = { 0 };
static uint32_t		windows_len = 0;
static xcb_screen_t 	*screen;
static xcb_drawable_t 	window;

static xcb_window_t	overview;

// Bar
static xcb_window_t	bar;
static bool		bar_visible = true;
static const uint32_t	bar_height = 15;
static xcb_gcontext_t 	bar_gc;

static xcb_gc_t
gc_font_get(const char *font_name,
		const uint32_t background_color,
		const uint32_t foreground_color,
		const xcb_window_t window)
{
	xcb_font_t font = xcb_generate_id(connection);
	xcb_void_cookie_t cookie_font = xcb_open_font_checked(connection,
			font, strlen(font_name), font_name);
	xcb_generic_error_t *error = xcb_request_check(connection, cookie_font);
	if (error)
	{
		fprintf(stderr, "ERROR: Cannot open font: %d\n", error->error_code);
		return -1;
	}

	xcb_gcontext_t gc = xcb_generate_id(connection);
	uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	uint32_t values[3] = {
		[0] = foreground_color,
		[1] = background_color,
		[2] = font
	};
	xcb_void_cookie_t cookie_gc = xcb_create_gc_checked(
			connection,
			gc,
			window,
			mask,
			values);

	error = xcb_request_check(connection, cookie_gc);
	if (error)
	{
		fprintf(stderr, "ERROR: Cannot create gc: %d\n", error->error_code);
		return -1;
	}

	cookie_font = xcb_close_font_checked(connection, font);
	error = xcb_request_check(connection, cookie_font);
	if (error)
	{
		fprintf(stderr, "ERROR: Cannot close font: %d\n", error->error_code);
		return -1;
	}

	return gc;
}

void
text_draw(const xcb_window_t window,
		const int16_t x,
		const int16_t y,
		const char *str,
		const uint32_t background_color,
		const uint32_t foreground_color)
{
	const uint8_t length = strlen(str);

	xcb_gcontext_t gc = gc_font_get(DEFAULT_FONT, background_color,
			foreground_color, window);
	if (gc == -1)
	{
		exit(-1);
	}

	xcb_void_cookie_t cookie_text = xcb_image_text_8_checked(
			connection,
			length,
			window,
			gc,
			x, y, str);

	xcb_generic_error_t *error = xcb_request_check(connection, cookie_text);
	if (error)
	{
		fprintf(stderr, "ERROR: Cannot paste text: %d\n", error->error_code);
		exit(-1);
	}

	xcb_void_cookie_t cookie_gc = xcb_free_gc(connection, gc);
	error = xcb_request_check(connection, cookie_gc);
	if (error)
	{
		fprintf(stderr, "ERROR: Cannot free gc: %d\n", error->error_code);
		exit(-1);
	}
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
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_BORDER_WIDTH,
			(uint32_t [1]) { 3 });

	uint32_t color = (focus) ? CONFIG_COLOR_FOCUS : CONFIG_COLOR_UNFOCUS;

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
			1920, bar_height,	// TODO: Use randr
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
				.width = 1920, // TODO: randr
				.height = bar_height
			} });

	int32_t index = find_window(window);
	if (index == -1)
	{
		return;
	}

	text_draw(bar, 5, bar_height - 3, windows[index].name,
			CONFIG_COLOR_BAR,
			CONFIG_COLOR_BAR_TEXT);
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

void
update_window_title(const xcb_window_t window, const char *title)
{
	int32_t index = find_window(window);
	if (index == -1)
	{
		return;
	}

	strcpy(windows[index].name, title);
}

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	printf("Running mmdtwm\n");

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

	setup_overview();
	setup_bar();

	xcb_flush(connection);

	uint32_t root_values[1] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_PROPERTY_CHANGE
		| XCB_EVENT_MASK_BUTTON_PRESS
	};

	xcb_generic_error_t *error = xcb_request_check(connection,
			xcb_change_window_attributes_checked(connection, root,
				XCB_CW_EVENT_MASK, root_values));

	if (error)
	{
		fprintf(stderr, "ERROR Cannot set root window attributes\n");
		free(error);
		goto exit_wm;
	}

	xcb_generic_event_t 		*ev;
	uint32_t 			values[3];
	xcb_get_geometry_reply_t	*geom;

	while ((ev = xcb_wait_for_event(connection)))
	{
		switch (ev->response_type & ~0x80)
		{
		case XCB_MAP_REQUEST:
		{	// New window
			xcb_map_request_event_t *e = (xcb_map_request_event_t *) ev;

			// TODO: Make a lists of windows to keep from those windows

			set_border(window, false);
			window = e->window;
			const uint32_t w_vals[1] = {
				XCB_EVENT_MASK_ENTER_WINDOW |
					XCB_EVENT_MASK_PROPERTY_CHANGE
			};

			xcb_change_window_attributes_checked(connection,
					window,
					XCB_CW_EVENT_MASK,
					w_vals);

			set_focus(window);
			set_border(window, true);

			// Add to list
			windows[windows_len].id = window;
			strcpy(windows[windows_len].name, get_name(window));
			windows[windows_len].visible = true;

			++windows_len;

			update_bar();

			printf("Mapping window: %s\n", get_name(window));

			xcb_map_window(connection, window);
			xcb_flush(connection);
		} break;
		case XCB_DESTROY_NOTIFY:
		{
			printf("Window destroyed\n");
		} break;
		case XCB_UNMAP_NOTIFY:
		{
			printf("Window unmapped\n");
		} break;
		case XCB_CONFIGURE_REQUEST:
		{
			//printf("Configure request\n");
		} break;
		case XCB_CONFIGURE_NOTIFY:
		{
			//printf("Configure notify\n");
		} break;
		case XCB_PROPERTY_NOTIFY:
		{
			//printf("Property notify\n");
			xcb_property_notify_event_t *e = (xcb_property_notify_event_t *) ev;

			//printf("Atom: %d == %d\n", e->atom, XCB_ATOM_WM_NAME);

			if (e->atom == XCB_ATOM_WM_NAME)
			{	// Window name changed
				update_window_title(e->window, get_name(e->window));
				update_bar();
			}
		} break;
		case XCB_CLIENT_MESSAGE:
		{
			// TODO
			printf("Client message\n");

#if 0
			xcb_client_message_event_t *e = (xcb_client_message_event_t *) ev;
			const xcb_window_t window = e->window;
			const xcb_atom_t atom = e->type;
			int32_t atom_index = -1;

			for (uint32_t i = 0; i < WM_ATOMS_ALL; ++i)
			{
				if (atom == wm_atoms[i])
				{
					atom_index = i;
					break;
				}
			}

			// Skip
			if (atom_index == -1)
			{
				break;
			}

			printf("Atom index: %d\n", atom_index);

			update_bar();

			switch (e->format)
			{
			case 8:
				e->data.data8;
				break;
			case 16:
				e->data.data16;
				break;
			case 32:
				e->data.data32;
				break;
			}
#endif
		} break;
		case XCB_KEY_PRESS:
		{
			xcb_key_press_event_t *e = (xcb_key_press_event_t *) ev;

			xcb_keysym_t keysym = xcb_key_symbols_get_keysym(syms, e->detail, 0);

			//printf("keysym got: %d | q: %d\n", keysym, XK_q);
			switch (keysym)
			{
			case XK_e:	// Exit window manager
				//printf("state: %d\n", e->state);
				if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
				{
					printf("Closing wm\n");
					goto exit_wm;
				}
				break;
			case XK_q:	// Kill window
				if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
				{
					send_event(e->child, wm_atoms[WM_ATOMS_DELETE]);
					update_bar();
					//xcb_kill_client(connection, window);
				}
				break;
			case XK_a:	// Raise window
				set_border(window, false);
				window = e->child;
				values[0] = XCB_STACK_MODE_ABOVE;
				xcb_configure_window(connection,
						window,
						XCB_CONFIG_WINDOW_STACK_MODE,
						values);

				set_focus(window);
				set_border(window, true);
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
		} break;
		case XCB_BUTTON_PRESS:
		{
			xcb_button_press_event_t *e = (xcb_button_press_event_t *) ev;

			// Ignore it (background)
			if (e->child == 0)
			{
				break;
			}

			// Set border of old one as un-focused
			set_border(window, false);

			window = e->child;

			// Raise window
			values[0] = XCB_STACK_MODE_ABOVE;
			xcb_configure_window(connection,
					window,
					XCB_CONFIG_WINDOW_STACK_MODE,
					values);

			set_border(window, true);
			update_bar();

			geom = xcb_get_geometry_reply(connection,
					xcb_get_geometry(connection, window),
					NULL);

			switch (e->detail)
			{
			case 1: // Move
				values[2] = 1;
				xcb_warp_pointer(connection, XCB_NONE, window, 0, 0, 0, 0, 1, 1);
				break;
			case 3: // Resize
				values[2] = 3;
				xcb_warp_pointer(connection, XCB_NONE, window, 0, 0, 0, 0, geom->width, geom->height);
				break;
			}

			free(geom);

			xcb_grab_pointer(connection, 0, root,
					XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
					root, XCB_NONE, XCB_CURRENT_TIME);

			xcb_flush(connection);
		} break;
		case XCB_ENTER_NOTIFY:
		{
			xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *) ev;

			set_border(window, false);
			window = e->event;
			set_border(window, true);
			update_bar();

			xcb_flush(connection);
		} break;
		case XCB_MOTION_NOTIFY:
		{
			xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(
					connection,
					xcb_query_pointer(connection, root),
					0);

			if (!pointer)
			{
				break;
			}

			const int16_t pointer_x = pointer->root_x;
			const int16_t pointer_y = pointer->root_y;

			//printf("motion: %d %d\n", pointer_x, pointer_y);

			switch (values[2])
			{
			case 1: // Move
			{
				geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), NULL);

				values[0] = (pointer_x + geom->width > screen->width_in_pixels) ? (screen->width_in_pixels - geom->width) : pointer_x;
				values[1] = (pointer_y + geom->height > screen->height_in_pixels) ? (screen->height_in_pixels - geom->height) : pointer_y;

				free(geom);

				//printf("Move value: %d %d\n", values[0], values[1]);
				xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
				xcb_flush(connection);
			} break;
			case 3: // Resize
			{
				geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), NULL);

				if ((pointer_x - geom->x) < MIN_WIDTH || (pointer_y - geom->y) < MIN_HEIGHT)
				{
					break;
				}

				values[0] = pointer_x - geom->x;
				values[1] = pointer_y - geom->y;

				free(geom);

				xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
				xcb_flush(connection);
			} break;
			}

			free(pointer);
		} break;
		case XCB_BUTTON_RELEASE:
		{
			xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
			xcb_flush(connection);
		} break;
		}

		free(ev);
	}

exit_wm:
	if (ev) free(ev);

	xcb_key_symbols_free(syms);
	xcb_set_input_focus(connection, XCB_NONE,
			XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_CURRENT_TIME);

	xcb_unmap_window(connection, bar);
	xcb_destroy_window(connection, bar);

	xcb_flush(connection);
	xcb_disconnect(connection);
	printf("Closing mmdtwm\n");

	return 0;
}

