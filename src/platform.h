#ifndef BUXN_PLATFORM_H
#define BUXN_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <sokol_app.h>

typedef int64_t (*read_fn_t)(void* handle, void* buffer, uint64_t num_bytes);

typedef struct {
	read_fn_t read;
	void* handle;
} stream_t;

typedef struct {
	int argc;
	const char** argv;
} args_t;

struct buxn_vm_s;

void
platform_init(args_t* args);

void
platform_cleanup(void);

sapp_icon_desc
platform_icon(void);

void
platform_init_log(void);

void
platform_init_fs(void);

void
platform_init_dbg(struct buxn_vm_s* vm);

void
platform_update_dbg(void);

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

#endif
