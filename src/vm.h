#ifndef BUXN_VM_H
#define BUXN_VM_H

#include <stdint.h>

#define BUXN_STACK_SIZE 256
#define BUXN_RESET_VECTOR 0x0100
#define BUXN_MEMORY_BANK_SIZE UINT16_MAX
#define BUXN_MAX_NUM_MEMORY_BANKS 16
#define BUXN_DEVICE_MEM_SIZE 256

#define BUXN_DEVICE_SYSTEM   0x00
#define BUXN_DEVICE_CONSOLE  0x01
#define BUXN_DEVICE_AUDIO_0  0x03
#define BUXN_DEVICE_AUDIO_1  0x04
#define BUXN_DEVICE_AUDIO_2  0x05
#define BUXN_DEVICE_AUDIO_3  0x06
#define BUXN_DEVICE_DATETIME 0x0c

#define BUXN_VM_RESET_NONE   0
#define BUXN_VM_RESET_ZERO_PAGE (1 << 1)
#define BUXN_VM_RESET_HIGH_MEM  (1 << 2)
#define BUXN_VM_RESET_DEVICE    (1 << 3)
#define BUXN_VM_RESET_STACK     (1 << 4)
#define BUXN_VM_RESET_ALL \
	(BUXN_VM_RESET_ZERO_PAGE \
	|BUXN_VM_RESET_HIGH_MEM \
	|BUXN_VM_RESET_DEVICE \
	|BUXN_VM_RESET_STACK)
#define BUXN_VM_RESET_SOFT (BUXN_RESET_HIGH_MEM | BUXN_RESET_STACK)

typedef struct buxn_vm_s {
	// User config, not touched by buxn
	// Everything else will be reset
	void* userdata;
	uint32_t memory_size;

	// VM state
	uint8_t wsp;
	uint8_t rsp;
	uint8_t ws[BUXN_STACK_SIZE];
	uint8_t rs[BUXN_STACK_SIZE];
	uint8_t device[BUXN_DEVICE_MEM_SIZE];
	uint8_t memory[];
} buxn_vm_t;

void
buxn_vm_reset(buxn_vm_t* vm, uint8_t reset_flags);

void
buxn_vm_execute(buxn_vm_t* vm, uint16_t vector);

static inline uint8_t
buxn_device_id(uint8_t address) {
	return (address & 0xf0) >> 4;
}

static inline uint8_t
buxn_vm_mem_load1(buxn_vm_t* vm, uint16_t addr) {
	return vm->memory[addr];
}

static inline uint16_t
buxn_vm_mem_load2(buxn_vm_t* vm, uint16_t addr) {
	uint16_t hi = (uint16_t)buxn_vm_mem_load1(vm, addr) << 8;
	uint16_t lo = (uint16_t)buxn_vm_mem_load1(vm, (addr + 1) & 0xffff);
	return hi | lo;
}

static inline uint8_t
buxn_vm_dev_load1(buxn_vm_t* vm, uint8_t addr) {
	return vm->device[addr];
}

static inline uint16_t
buxn_vm_dev_load2(buxn_vm_t* vm, uint8_t addr) {
	uint16_t hi = (uint16_t)buxn_vm_dev_load1(vm, addr) << 8;
	uint16_t lo = (uint16_t)buxn_vm_dev_load1(vm, (addr + 1) & 0xff);
	return hi | lo;
}

// Must be provided by the host program

extern uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address);

extern void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address);

#endif
