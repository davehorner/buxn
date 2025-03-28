#ifndef BUXN_VM_H
#define BUXN_VM_H

#include <stdint.h>
#include <stddef.h>

#define BUXN_STACK_SIZE 256
#define BUXN_RESET_VECTOR 0x0100

typedef struct {
	void* userdata;
	uint16_t pc;
	uint16_t wsp;
	uint16_t rsp;
	uint8_t ws[BUXN_STACK_SIZE];
	uint8_t rs[BUXN_STACK_SIZE];
	uint8_t memory[];
} buxn_vm_t;

typedef uint16_t
buxn_vm_dei_t(buxn_vm_t* vm, uint8_t address);

typedef void
buxn_vm_deo_t(buxn_vm_t* vm, uint8_t address, uint16_t value);

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

extern buxn_vm_dei_t buxn_vm_dei;
extern buxn_vm_deo_t buxn_vm_deo;

#endif
