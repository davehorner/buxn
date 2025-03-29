#include "stdio_console.h"
#include "../vm.h"

static inline void
buxn_send_data(
	buxn_vm_t* vm,
	buxn_stdio_console_t* device,
	uint8_t type,
	uint8_t value
) {
	device->type = type;
	device->value = value;

	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x10);
	buxn_vm_execute(vm, vector_addr);
}

void
buxn_stdio_console_init(
	struct buxn_vm_s* vm,
	buxn_stdio_console_t* device,
	int argc, const char** argv
) {
	(void)vm;

	device->argc = argc;
	device->argv = argv;
	device->type = argc > 0;
}

uint8_t
buxn_stdio_console_dei(
	struct buxn_vm_s* vm,
	buxn_stdio_console_t* device,
	uint8_t address
) {
	switch (address) {
		case 0x12: return device->value;
		case 0x17: return device->type;
		default: return vm->device[address];
	}
}

void
buxn_stdio_console_deo(
	struct buxn_vm_s* vm,
	buxn_stdio_console_t* device,
	uint8_t address
) {
	switch (address) {
		case 0x18:
			if (device->write) {
				fputc(vm->device[address], device->write);
			}
			break;
		case 0x19:
			if (device->error) {
				fputc(vm->device[address], device->error);
			}
			break;
	}
}

void
buxn_stdio_console_send_args(struct buxn_vm_s* vm, buxn_stdio_console_t* device) {
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

bool
buxn_stdio_console_should_update_io(struct buxn_vm_s* vm, buxn_stdio_console_t* device) {
	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x10);
	return device->read != NULL && !feof(device->read) && vector_addr != 0;
}

void
buxn_stdio_console_update_io(struct buxn_vm_s* vm, buxn_stdio_console_t* device) {
	if (device->read == NULL) { return; }

	int ch = fgetc(device->read);
	if (ch == EOF) {
		buxn_send_data(vm, device, 4, 0);
	} else {
		buxn_send_data(vm, device, 1, ch);
	}
}
