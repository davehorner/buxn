#include "sokol_console.h"
#include "sokol_log.h"
#include "../vm.h"
#include <stdbool.h>

static inline void
buxn_send_data(
	buxn_vm_t* vm,
	buxn_sokol_console_t* device,
	uint8_t type,
	uint8_t value
) {
	device->type = type;
	device->value = value;

	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x10);
	buxn_vm_execute(vm, vector_addr);
}

static inline void
buxn_sokol_console_putc(
	int level,
	buxn_sokol_console_buf_t* buffer,
	char ch
) {
	bool should_flush = false;
	if (ch != '\n') {
		buffer->data[buffer->pos++] = ch;
		should_flush = buffer->pos >= (int)(sizeof(buffer->data) - 1);
	} else {
		should_flush = true;
	}

	if (should_flush) {
		buffer->data[buffer->pos] = '\0';
		slog_func("uxn", level, level, buffer->data, __LINE__, __FILE__, 0);
		buffer->pos = 0;
	}
}

void
buxn_sokol_console_init(
	struct buxn_vm_s* vm,
	buxn_sokol_console_t* device,
	int argc, const char** argv
) {
	(void)vm;

	device->argc = argc;
	device->argv = argv;
	device->type = argc > 0;
}

void
buxn_sokol_console_send_args(struct buxn_vm_s* vm, buxn_sokol_console_t* device) {
	while (device->argc > 0) {
		const char* ch = &device->argv[0][0];

		while (1) {
			char arg_ch = *ch;

			if (arg_ch != 0) {
				buxn_send_data(vm, device, 2, arg_ch);
			} else if (device->argc == 1) {
				buxn_send_data(vm, device, 4, 0);
			} else {
				buxn_send_data(vm, device, 3, 0);
			}

			if (arg_ch != 0) {
				++ch;
			} else {
				--device->argc;
				++device->argv;
				break;
			}
		}
	}
}

uint8_t
buxn_sokol_console_dei(struct buxn_vm_s* vm, buxn_sokol_console_t* device, uint8_t address) {
	switch (address) {
		case 0x12: return device->value;
		case 0x17: return device->type;
		default: return vm->device[address];
	}
}

void
buxn_sokol_console_deo(struct buxn_vm_s* vm, buxn_sokol_console_t* device, uint8_t address) {
	switch (address) {
		case 0x18: buxn_sokol_console_putc(3, &device->info_buf, vm->device[address]); break;
		case 0x19: buxn_sokol_console_putc(1,&device->error_buf, vm->device[address]); break;
	}
}
