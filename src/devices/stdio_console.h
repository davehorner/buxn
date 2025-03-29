#ifndef BUXN_DEVICE_STDIO_CONSOLE_H
#define BUXN_DEVICE_STDIO_CONSOLE_H

#include <stdio.h>
#include <stdint.h>

struct buxn_vm_s;

typedef struct {
	FILE* read;
	FILE* write;
	FILE* error;

	uint16_t vector_addr;
	int argc;
	const char** argv;
	uint8_t type;
	uint8_t value;
} buxn_stdio_console_t;

void
buxn_stdio_console_init(
	struct buxn_vm_s* vm,
	buxn_stdio_console_t* device,
	int argc, const char** argv
);

uint8_t
buxn_stdio_console_dei(struct buxn_vm_s* vm, buxn_stdio_console_t* device, uint8_t address);

void
buxn_stdio_console_deo(struct buxn_vm_s* vm, buxn_stdio_console_t* device, uint8_t address);

void
buxn_stdio_console_send_args(struct buxn_vm_s* vm, buxn_stdio_console_t* device);

void
buxn_stdio_console_update_io(struct buxn_vm_s* vm, buxn_stdio_console_t* device);

#endif
