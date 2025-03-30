#ifndef BUXN_DEVICE_SOKOL_CONSOLE_H
#define BUXN_DEVICE_SOKOL_CONSOLE_H

#include <stdint.h>

#define BUXN_SOKOL_CONSOLE_BUFFER_SIZE 256

struct buxn_vm_s;

typedef struct {
	int pos;
	char data[BUXN_SOKOL_CONSOLE_BUFFER_SIZE];
} buxn_sokol_console_buf_t;

typedef struct {
	int argc;
	const char** argv;
	uint8_t type;
	uint8_t value;

	buxn_sokol_console_buf_t info_buf;
	buxn_sokol_console_buf_t error_buf;
} buxn_sokol_console_t;

void
buxn_sokol_console_init(
	struct buxn_vm_s* vm,
	buxn_sokol_console_t* device,
	int argc, const char** argv
);

uint8_t
buxn_sokol_console_dei(struct buxn_vm_s* vm, buxn_sokol_console_t* device, uint8_t address);

void
buxn_sokol_console_deo(struct buxn_vm_s* vm, buxn_sokol_console_t* device, uint8_t address);

void
buxn_sokol_console_send_args(struct buxn_vm_s* vm, buxn_sokol_console_t* device);

#endif
