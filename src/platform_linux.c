#include "platform.h"
#include <blog.h>
#include <sokol_app.h>
#include <string.h>
#include <stdlib.h>
#include <physfs.h>
#include <X11/Xlib.h>

static blog_file_logger_options_t options = { 0 };

void
platform_init_log(void) {
	options.file = stderr;
	options.with_colors = true;

#ifdef _DEBUG
	blog_add_file_logger(BLOG_LEVEL_TRACE, &options);
#else
	blog_add_file_logger(BLOG_LEVEL_INFO, &options);
#endif
}

void
platform_init_fs(int argc, const char* argv[]) {
	(void)argc;
	PHYSFS_init(argv[0]);
	PHYSFS_setWriteDir(".");

	if (argc >= 2) {
		int len = (int)strlen(argv[1]);
		int i;
		for (i = len - 1; i >= 0; --i) {
			if (argv[1][i] == '/' || argv[1][i] == '\\') {
				break;
			}
		}

		if (i > 0) {
			char* rom_dir = malloc(i + 1);
			memcpy(rom_dir, argv[1], i);
			rom_dir[i] = '\0';

			BLOG_INFO("Mounting %s to /", rom_dir);
			if (!PHYSFS_mount(rom_dir, "/", 1)) {
				BLOG_ERROR("Error while mounting: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
			}
			free(rom_dir);
		}
	}

	PHYSFS_mount(".", "/", 1);
}

void
platform_resize_window(uint16_t width, uint16_t height) {
	Display* display = (Display*)sapp_x11_get_display();
	Window window = (Window)sapp_x11_get_window();
	XResizeWindow(display, window, width, height);
}
