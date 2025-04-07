#ifndef BUXN_DEVICE_CONSOLE_H
#define BUXN_DEVICE_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

#define BUXN_CONSOLE_STDIN    0x1
#define BUXN_CONSOLE_ARG      0x2
#define BUXN_CONSOLE_ARG_SEP  0x3
#define BUXN_CONSOLE_END      0x4

typedef struct {
	int argc;
	const char** argv;
	uint8_t type;
	uint8_t value;
} buxn_console_t;

struct buxn_vm_s;

void
buxn_console_init(struct buxn_vm_s* vm, buxn_console_t* device, int argc, const char** argv);

uint8_t
buxn_console_dei(struct buxn_vm_s* vm, buxn_console_t* device, uint8_t address);

void
buxn_console_deo(struct buxn_vm_s* vm, buxn_console_t* device, uint8_t address);

void
buxn_console_send_args(struct buxn_vm_s* vm, buxn_console_t* device);

bool
buxn_console_should_send_input(struct buxn_vm_s* vm);

void
buxn_console_send_input(struct buxn_vm_s* vm, buxn_console_t* device, char c);

void
buxn_console_send_input_end(struct buxn_vm_s* vm, buxn_console_t* device);

// Must be provided by the host program

extern void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c);

extern void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c);

#endif
