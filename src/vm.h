#ifndef BUXN_VM_H
#define BUXN_VM_H

#include <stdint.h>
#include <stddef.h>

#define BUXN_STACK_SIZE 256
#define BUXN_RESET_VECTOR 0x0100
#define BUXN_MEMORY_BANK_SIZE UINT16_MAX

typedef struct buxn_vm_s {
	// User config, not touched by buxn
	// Everything else will be reset
	void* userdata;
	uint32_t memory_size;

	// System device state
	void (*debug_hook)(struct buxn_vm_s* vm, uint8_t value);
	uint16_t metadata_addr;
	uint16_t color_r, color_g, color_b;
	uint8_t state;

	// VM state
	uint16_t pc;
	uint16_t wsp;
	uint16_t rsp;
	uint8_t ws[BUXN_STACK_SIZE];
	uint8_t rs[BUXN_STACK_SIZE];
	uint8_t memory[];
} buxn_vm_t;

void
buxn_vm_reset(buxn_vm_t* vm);

int
buxn_vm_execute(buxn_vm_t* vm);

static inline uint8_t
buxn_device_id(uint8_t address) {
	return (address & 0xf0) >> 4;
}

static inline uint8_t
buxn_device_port(uint8_t address) {
	return address & 0x0f;
}

static inline uint16_t
buxn_vm_load2(buxn_vm_t* vm, uint16_t addr) {
	return ((uint16_t)vm->memory[addr] << 8) | ((uint16_t)vm->memory[addr + 1]);
}

// Must be provided by host program

extern uint16_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address);

extern void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address, uint16_t value);

#endif
