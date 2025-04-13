#ifndef BUXN_VM_H
#define BUXN_VM_H

#include <stdint.h>

#define BUXN_STACK_SIZE 256
#define BUXN_RESET_VECTOR 0x0100
#define BUXN_MEMORY_BANK_SIZE ((size_t)UINT16_MAX + 1)
#define BUXN_MAX_NUM_MEMORY_BANKS 16
#define BUXN_DEVICE_MEM_SIZE 256

#define BUXN_DEVICE_SYSTEM     0x00
#define BUXN_DEVICE_CONSOLE    0x10
#define BUXN_DEVICE_SCREEN     0x20
#define BUXN_DEVICE_AUDIO_0    0x30
#define BUXN_DEVICE_AUDIO_1    0x40
#define BUXN_DEVICE_AUDIO_2    0x50
#define BUXN_DEVICE_AUDIO_3    0x60
#define BUXN_DEVICE_CONTROLLER 0x80
#define BUXN_DEVICE_MOUSE      0x90
#define BUXN_DEVICE_FILE_0     0xa0
#define BUXN_DEVICE_FILE_1     0xb0
#define BUXN_DEVICE_DATETIME   0xc0
#define BUXN_NUM_AUDIO_DEVICES 4
#define BUXN_NUM_FILE_DEVICES  2

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

#define BUXN_MEM_ADDR_MASK 0xffff
#define BUXN_DEV_ADDR_MASK 0x00ff
#define BUXN_DEV_PRIV_ADDR_MASK 0x000f

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
	return address & 0xf0;
}

static inline uint8_t
buxn_device_port(uint8_t address) {
	return address & 0x0f;
}

static inline uint16_t
buxn_vm_load2(uint8_t* mem, uint16_t addr, uint16_t addr_mask) {
	uint16_t hi = (uint16_t)mem[(addr + 0) & addr_mask] << 8;
	uint16_t lo = (uint16_t)mem[(addr + 1) & addr_mask];
	return hi | lo;
}

static inline uint16_t
buxn_vm_mem_load2(buxn_vm_t* vm, uint16_t addr) {
	return buxn_vm_load2(vm->memory, addr, BUXN_MEM_ADDR_MASK);
}

static inline uint16_t
buxn_vm_dev_load2(buxn_vm_t* vm, uint8_t addr) {
	return buxn_vm_load2(vm->device, addr, BUXN_DEV_ADDR_MASK);
}

// Must be provided by the host program

extern uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address);

extern void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address);

#endif
