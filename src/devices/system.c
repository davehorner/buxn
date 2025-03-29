#include "system.h"
#include "../vm.h"
#include <string.h>

int
buxn_system_exit_code(struct buxn_vm_s* vm) {
	uint8_t state = vm->device[0x0f];
	return state == 0 ? -1 : state & 0x7f;
}

uint8_t
buxn_system_dei(struct buxn_vm_s* vm, buxn_system_t* device, uint8_t address) {
	(void)device;
	switch (address) {
		case 0x04: return vm->wsp;
		case 0x05: return vm->rsp;
		default: return vm->device[address];
	}
}

void
buxn_system_deo(struct buxn_vm_s* vm, buxn_system_t* device, uint8_t address) {
	switch (address) {
		case 0x02: {
			uint16_t value = buxn_vm_dev_load2(vm, address);
			uint8_t op = vm->memory[value];
			uint32_t memory_size = vm->memory_size;
			uint32_t length = buxn_vm_mem_load2(vm, value + 1);
			switch (op) {
				case 0x00: {
					uint32_t bank = buxn_vm_mem_load2(vm, value + 3);
					uint32_t addr = buxn_vm_mem_load2(vm, value + 5);
					uint32_t start = bank * (uint32_t)UINT16_MAX + addr;
					start = start < memory_size ? start : memory_size;

					uint32_t end = start + length;
					end = end < memory_size ? end : memory_size;

					uint8_t fill_value = vm->memory[value + 7];
					memset(vm->memory + start, fill_value, end - start);
				} break;
				case 0x01:
				case 0x02: {
					uint32_t src_bank = buxn_vm_mem_load2(vm, value + 3);
					uint32_t src_addr = buxn_vm_mem_load2(vm, value + 5);
					uint32_t src = src_bank * UINT16_MAX + src_addr;
					src = src < memory_size ? src : memory_size;

					uint32_t dst_bank = buxn_vm_mem_load2(vm, value + 7);
					uint32_t dst_addr = buxn_vm_mem_load2(vm, value + 9);
					uint32_t dst = dst_bank * UINT16_MAX + dst_addr;
					dst = dst < memory_size ? dst : memory_size;

					uint32_t max = src > dst ? src : dst;
					uint32_t end = max + length;
					end = end < memory_size ? end : memory_size;
					length = end - max;

					if (op == 0x01) {
						memcpy(vm->memory + dst, vm->memory + src, length);
					} else {
						memmove(vm->memory + dst, vm->memory + src, length);
					}
				} break;
			}
		} break;
		case 0x04: vm->wsp = vm->device[address]; break;
		case 0x05: vm->rsp = vm->device[address]; break;
		case 0x0e: if (device->debug_hook) { device->debug_hook(vm); } break;
	}
}
