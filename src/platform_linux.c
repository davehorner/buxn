#define _GNU_SOURCE
#include "platform.h"
#include <blog.h>
#include <sokol_app.h>
#include <stdio.h>
#include <physfs.h>
#include <errno.h>
#include <string.h>
#include <X11/Xlib.h>
#include <unistd.h>
#include <poll.h>

static struct {
	blog_file_logger_options_t log_options;
	const char* argv0;
	const char* boot_rom_file;
} platform_linux = { 0 };

static int64_t
stdio_read(void* handle, void* buffer, uint64_t size) {
	return fread(buffer, 1, size, handle);
}

void
platform_parse_args(args_t* args) {
	if (args->argc > 0) {
		platform_linux.argv0 = args->argv[0];
		++args->argv;
		--args->argc;
	}

	if (args->argc > 0) {
		platform_linux.boot_rom_file = args->argv[0];
		++args->argv;
		--args->argc;
	}
}

void
platform_init_log(void) {
	platform_linux.log_options.file = stderr;
	platform_linux.log_options.with_colors = isatty(fileno(stderr));

#ifdef _DEBUG
	blog_add_file_logger(BLOG_LEVEL_TRACE, &platform_linux.log_options);
#else
	blog_add_file_logger(BLOG_LEVEL_INFO, &platform_linux.log_options);
#endif
}

void
platform_init_fs(void) {
	PHYSFS_init(platform_linux.argv0);
	PHYSFS_setWriteDir(".");
	PHYSFS_mount(".", "/", 1);
}

bool
platform_open_boot_rom(stream_t* stream) {
	FILE* rom_file = fopen(platform_linux.boot_rom_file, "rb");
	if (rom_file == NULL) {
		BLOG_ERROR("Could not open rom file: %s", strerror(errno));
		return false;
	}

	stream->handle = rom_file;
	stream->read = stdio_read;
	return true;
}

const char*
platform_stream_error(stream_t stream) {
	(void)stream;
	return strerror(errno);
}

void
platform_close_stream(stream_t stream) {
	fclose(stream.handle);
}

void
platform_resize_window(uint16_t width, uint16_t height) {
	Display* display = (Display*)sapp_x11_get_display();
	Window window = (Window)sapp_x11_get_window();
	XResizeWindow(display, window, width, height);
}

int
platform_poll_stdin(char* ch, int size) {
	int fd = fileno(stdin);
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN,
	};

	if (poll(&pfd, 1, 0) > 0) {
		int c = read(fd, ch, size);
		if (c == 0) {
			return -1;
		} else {
			return c;
		}
	} else {
		return 0;
	}
}
