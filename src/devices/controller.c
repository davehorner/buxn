#include "controller.h"
#include "../vm.h"

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
buxn_controller_send_event(struct buxn_vm_s* vm) {
	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x80);
	if (vector_addr != 0) { buxn_vm_execute(vm, vector_addr); }
}
