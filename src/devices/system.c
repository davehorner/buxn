#include "system.h"
#include "../vm/vm.h"
#include <string.h>

int
buxn_system_exit_code(struct buxn_vm_s* vm) {
	uint8_t state = vm->device[0x0f];
	return state == 0 ? -1 : state & 0x7f;
}

uint8_t
buxn_system_dei(struct buxn_vm_s* vm, uint8_t address) {
	switch (address) {
		case 0x04: return vm->wsp;
		case 0x05: return vm->rsp;
		default: return vm->device[address];
	}
}

void
buxn_system_deo(struct buxn_vm_s* vm, uint8_t address) {
	switch (address) {
		case 0x03: {
			uint16_t op_addr = buxn_vm_dev_load2(vm, 0x02);
			uint8_t op = vm->memory[op_addr];
			uint32_t memory_size = vm->memory_size;
			uint32_t length = buxn_vm_mem_load2(vm, op_addr + 1);
			switch (op) {
				case 0x00: {
					uint32_t bank = buxn_vm_mem_load2(vm, op_addr + 3);
					uint32_t addr = buxn_vm_mem_load2(vm, op_addr + 5);
					uint32_t start = bank * ((uint32_t)UINT16_MAX + 1) + addr;
					start = start < memory_size ? start : memory_size;

					uint32_t end = start + length;
					end = end < memory_size ? end : memory_size;

					uint8_t fill_value = vm->memory[op_addr + 7];
					memset(vm->memory + start, fill_value, end - start);
				} break;
				case 0x01:
				case 0x02: {
					uint32_t src_bank = buxn_vm_mem_load2(vm, op_addr + 3);
					uint32_t src_addr = buxn_vm_mem_load2(vm, op_addr + 5);
					uint32_t src = src_bank * ((uint32_t)UINT16_MAX + 1) + src_addr;
					src = src < memory_size ? src : memory_size;

					uint32_t dst_bank = buxn_vm_mem_load2(vm, op_addr + 7);
					uint32_t dst_addr = buxn_vm_mem_load2(vm, op_addr + 9);
					uint32_t dst = dst_bank * ((uint32_t)UINT16_MAX + 1) + dst_addr;
					dst = dst < memory_size ? dst : memory_size;

					uint32_t max = src > dst ? src : dst;
					uint32_t end = max + length;
					end = end < memory_size ? end : memory_size;
					length = end - max;

					if (op == 0x01) {
						for (uint32_t i = 0; i < length; ++i) {
							vm->memory[dst + i] = vm->memory[src + i];
						}
					} else {
						for (uint32_t i = 1; i <= length; ++i) {
							vm->memory[dst + length - i] = vm->memory[src + length - i];
						}
					}
				} break;
			}
		} break;
		case 0x04: vm->wsp = vm->device[address]; break;
		case 0x05: vm->rsp = vm->device[address]; break;
		case 0x07:
			buxn_system_set_metadata(vm, buxn_vm_dev_load2(vm, 0x06));
			break;
		case 0x09:
		case 0x0b:
		case 0x0d:
			buxn_system_theme_changed(vm);
			break;
		case 0x0e:
			if (vm->device[address]) {
				buxn_system_debug(vm, vm->device[address]);
			}
			break;
	}
}

static uint32_t
buxn_make_rgba(uint8_t r, uint8_t g, uint8_t b) {
	return (uint32_t)0xff000000
		 | ((uint32_t)b << 20) | ((uint32_t)b << 16)
	     | ((uint32_t)g << 12) | ((uint32_t)g <<  8)
		 | ((uint32_t)r <<  4) | ((uint32_t)r <<  0);
}

void
buxn_system_palette(struct buxn_vm_s* vm, uint32_t palette[4]) {
	uint16_t r = buxn_vm_dev_load2(vm, 0x08);
	uint16_t g = buxn_vm_dev_load2(vm, 0x0a);
	uint16_t b = buxn_vm_dev_load2(vm, 0x0c);

	palette[0] = buxn_make_rgba((r >> 12) & 0x0f, (g >> 12) & 0x0f, (b >> 12) & 0x0f);
	palette[1] = buxn_make_rgba((r >>  8) & 0x0f, (g >>  8) & 0x0f, (b >>  8) & 0x0f);
	palette[2] = buxn_make_rgba((r >>  4) & 0x0f, (g >>  4) & 0x0f, (b >>  4) & 0x0f);
	palette[3] = buxn_make_rgba((r >>  0) & 0x0f, (g >>  0) & 0x0f, (b >>  0) & 0x0f);
}
