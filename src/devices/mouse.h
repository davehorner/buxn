#ifndef BUXN_DEVICE_MOUSE_H
#define BUXN_DEVICE_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

struct buxn_vm_s;

typedef struct {
	uint16_t x;
	uint16_t y;
	uint8_t state;
	int16_t scroll_x;
	int16_t scroll_y;
} buxn_mouse_t;

uint8_t
buxn_mouse_dei(struct buxn_vm_s* vm, buxn_mouse_t* device, uint8_t address);

void
buxn_mouse_deo(struct buxn_vm_s* vm, buxn_mouse_t* device, uint8_t address);

void
buxn_mouse_update(struct buxn_vm_s* vm);

static inline void
buxn_mouse_set_button(buxn_mouse_t* device, uint8_t button, bool down) {
	uint8_t mask = 1 << button;
	if (down) {
		device->state |= mask;
	} else {
		device->state &= ~mask;
	}
}

static inline bool
buxn_mouse_check_button(buxn_mouse_t* device, uint8_t button) {
	uint8_t mask = 1 << button;
	return (device->state & mask) > 0;
}

#endif
