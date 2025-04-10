#ifndef BUXN_PLATFORM_H
#define BUXN_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

typedef int64_t (*read_fn_t)(void* handle, void* buffer, uint64_t num_bytes);

typedef struct {
	read_fn_t read;
	void* handle;
} stream_t;

typedef struct {
	int argc;
	const char** argv;
} args_t;

// TODO: rename this to: platform_init and and platform_cleanup
void
platform_parse_args(args_t* args);

void
platform_init_log(void);

void
platform_init_fs(void);

bool
platform_open_boot_rom(stream_t* stream);

const char*
platform_stream_error(stream_t stream);

void
platform_close_stream(stream_t stream);

void
platform_resize_window(uint16_t width, uint16_t height);

int
platform_poll_stdin(char* ch, int size);

float
platform_render_scale(void);

#endif
