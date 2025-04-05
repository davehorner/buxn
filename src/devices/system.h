#ifndef BUXN_DEVICE_SYSTEM_H
#define BUXN_DEVICE_SYSTEM_H

#include <stdint.h>

struct buxn_vm_s;

int
buxn_system_exit_code(struct buxn_vm_s* vm);

void
buxn_system_palette(struct buxn_vm_s* vm, uint32_t palette[4]);

uint8_t
buxn_system_dei(struct buxn_vm_s* vm, uint8_t address);

void
buxn_system_deo(struct buxn_vm_s* vm, uint8_t address);

// Must be provided by the host program

extern void
buxn_system_debug(struct buxn_vm_s* vm, uint8_t value);

extern void
buxn_system_set_metadata(struct buxn_vm_s* vm, uint16_t address);

extern void
buxn_system_theme_changed(struct buxn_vm_s* vm);

#endif
