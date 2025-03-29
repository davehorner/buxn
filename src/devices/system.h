#ifndef BUXN_DEVICE_SYSTEM_H
#define BUXN_DEVICE_SYSTEM_H

#include <stdint.h>

struct buxn_vm_s;

typedef struct {
	void (*debug_hook)(struct buxn_vm_s* vm);
} buxn_system_t;

int
buxn_system_exit_code(struct buxn_vm_s* vm);

uint8_t
buxn_system_dei(struct buxn_vm_s* vm, buxn_system_t* device, uint8_t address);

void
buxn_system_deo(struct buxn_vm_s* vm, buxn_system_t* device, uint8_t address);

#endif
