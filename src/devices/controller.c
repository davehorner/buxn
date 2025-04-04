#include "controller.h"
#include "../vm.h"
#include <stdio.h>
static void
buxn_controller_call_vector(struct buxn_vm_s* vm) {
	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x80);
	if (vector_addr != 0) { buxn_vm_execute(vm, vector_addr); }
}

uint8_t
buxn_controller_dei(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address) {
	switch (address) {
		case 0x82: return device->buttons[0];
		case 0x83: return device->ch;
		case 0x85: return device->buttons[1];
		case 0x86: return device->buttons[2];
		case 0x87: return device->buttons[3];
		default: return vm->device[address];
	}
}

void
buxn_controller_deo(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address) {
	(void)vm;
	(void)device;
	(void)address;
}

void
buxn_controller_set_button(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	int controller_index,
	buxn_controller_btn_t btn,
	bool down
) {
	uint8_t mask = 1 << btn;
	if (down) {
		device->buttons[controller_index] |= mask;
	} else {
		device->buttons[controller_index] &= ~mask;
	}

	buxn_controller_call_vector(vm);
}

void
buxn_controller_send_char(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	char ch
) {
	device->ch = ch;
	buxn_controller_call_vector(vm);
	device->ch = 0;
}
