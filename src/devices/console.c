#include "console.h"
#include "../vm.h"

static inline void
buxn_console_send_data(
	buxn_vm_t* vm,
	buxn_console_t* device,
	uint8_t type,
	uint8_t value
) {
	device->type = type;
	device->value = value;

	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x10);
	buxn_vm_execute(vm, vector_addr);
}

void
buxn_console_init(struct buxn_vm_s* vm, buxn_console_t* device, int argc, const char** argv) {
	(void)vm;

	device->argc = argc;
	device->argv = argv;
	device->type = argc > 0;
}

void
buxn_console_send_args(struct buxn_vm_s* vm, buxn_console_t* device) {
	while (device->argc > 0) {
		const char* ch = &device->argv[0][0];

		while (1) {
			char arg_ch = *ch;

			if (arg_ch != 0) {
				buxn_console_send_data(vm, device, 2, arg_ch);
			} else if (device->argc == 1) {
				buxn_console_send_data(vm, device, 4, 0);
			} else {
				buxn_console_send_data(vm, device, 3, 0);
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
buxn_console_dei(struct buxn_vm_s* vm, buxn_console_t* device, uint8_t address) {
	switch (address) {
		case 0x12: return device->value;
		case 0x17: return device->type;
		default: return vm->device[address];
	}
}

void
buxn_console_deo(struct buxn_vm_s* vm, buxn_console_t* device, uint8_t address) {
	switch (address) {
		case 0x18: buxn_console_handle_write(vm, device, vm->device[address]); break;
		case 0x19: buxn_console_handle_error(vm, device, vm->device[address]); break;
	}
}

bool
buxn_console_should_send_input(struct buxn_vm_s* vm) {
	return buxn_vm_dev_load2(vm, 0x10) != 0;
}

void
buxn_console_send_input(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	buxn_console_send_data(vm, device, 1, c);
}

void
buxn_console_send_input_end(struct buxn_vm_s* vm, buxn_console_t* device) {
	buxn_console_send_data(vm, device, 4, 0);
}
