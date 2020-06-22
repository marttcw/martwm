#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <sys/types.h> 
#include <unistd.h>
#include <stdbool.h>

const int32_t MIN_WIDTH = 20;
const int32_t MIN_HEIGHT = 20;

// Mask 1 = Alt key
// Mask 4 = Super key
#define PRIMARY_MOD_KEY XCB_MOD_MASK_4

enum {
	WM_ATOMS_PROTOCOLS, 
	WM_ATOMS_DELETE,
	WM_ATOMS_STATE,
	WM_ATOMS_TAKEFOCUS,

	WM_ATOMS_ALL
};

static xcb_connection_t *connection = NULL;
static xcb_drawable_t root;
static xcb_key_symbols_t *syms;
static xcb_atom_t wm_atoms[WM_ATOMS_ALL];

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
setup_key(const xcb_keysym_t keysym)
{
	xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(syms, keysym);

	xcb_grab_key(connection, 1, root,
			PRIMARY_MOD_KEY, keycode[0],
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	free(keycode);
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

	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
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

	xcb_flush(connection);

	xcb_generic_event_t 		*ev;
	uint32_t 			values[3];
	xcb_drawable_t 			window;
	xcb_get_geometry_reply_t	*geom;

	while ((ev = xcb_wait_for_event(connection)))
	{
		switch (ev->response_type & ~0x80)
		{
#if 0
		case XCB_EXPOSE:
		{
			xcb_change_gc
		} break;
#endif
		case XCB_KEY_PRESS:
		{
			xcb_key_press_event_t *e = (xcb_key_press_event_t *) ev;

			xcb_keysym_t keysym = xcb_key_symbols_get_keysym(syms, e->detail, 0);

			window = e->child;

			//printf("keysym got: %d | q: %d\n", keysym, XK_q);
			switch (keysym)
			{
			case XK_q:	// Exit window manager
				//printf("state: %d\n", e->state);
				if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
				{
					printf("Closing wm\n");
					goto exit_wm;
				}
				break;
			case XK_c:	// Kill window
				if (e->state == (PRIMARY_MOD_KEY | XCB_MOD_MASK_SHIFT))
				{
					send_event(window, wm_atoms[WM_ATOMS_DELETE]);
					//xcb_kill_client(connection, window);
				}
				break;
			case XK_a:	// Raise window
				values[0] = XCB_STACK_MODE_ABOVE;
				xcb_configure_window(connection,
						window,
						XCB_CONFIG_WINDOW_STACK_MODE,
						values);

				xcb_set_input_focus(connection,
						XCB_INPUT_FOCUS_PARENT,
						window,
						XCB_CURRENT_TIME);
				break;
			case XK_d:	// dmenu
				spawn("dmenu_run");
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

			window = e->child;

			// Raise window
			values[0] = XCB_STACK_MODE_ABOVE;
			xcb_configure_window(connection,
					window,
					XCB_CONFIG_WINDOW_STACK_MODE,
					values);

			// Set border
			xcb_configure_window(connection,
					window,
					XCB_CONFIG_WINDOW_BORDER_WIDTH,
					(uint32_t [1]) { 3 });

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

			free(pointer);

			switch (values[2])
			{
			case 1: // Move
			{
				geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), NULL);

				values[0] = (pointer_x + geom->width > screen->width_in_pixels) ? (screen->width_in_pixels - geom->width) : pointer_x;
				values[1] = (pointer_y + geom->height > screen->height_in_pixels) ? (screen->height_in_pixels - geom->height) : pointer_y;

				free(geom);

				printf("Move value: %d %d\n", values[0], values[1]);
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
	free(ev);
	if (geom) free(geom);

	xcb_key_symbols_free(syms);
	xcb_set_input_focus(connection, XCB_NONE,
			XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_CURRENT_TIME);

	xcb_flush(connection);
	xcb_disconnect(connection);
	printf("Closing mmdtwm\n");

	return 0;
}

