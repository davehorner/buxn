#ifndef BUXN_DEVICE_CONTROLLER_H
#define BUXN_DEVICE_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#define BUXN_NUM_CONTROLLERS 4

struct buxn_vm_s;

typedef struct {
	uint8_t buttons[BUXN_NUM_CONTROLLERS];
	uint8_t ch;
} buxn_controller_t;

typedef enum {
	BUXN_CONTROLLER_BTN_A,
	BUXN_CONTROLLER_BTN_B,
	BUXN_CONTROLLER_BTN_SELECT,
	BUXN_CONTROLLER_BTN_START,
	BUXN_CONTROLLER_BTN_UP,
	BUXN_CONTROLLER_BTN_DOWN,
	BUXN_CONTROLLER_BTN_LEFT,
	BUXN_CONTROLLER_BTN_RIGHT,
} buxn_controller_btn_t;

uint8_t
buxn_controller_dei(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address);

void
buxn_controller_deo(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address);

void
buxn_controller_send_event(struct buxn_vm_s* vm);

static inline void
buxn_controller_set_button(
	buxn_controller_t* device,
	int controller_index,
	buxn_controller_btn_t btn,
	bool down
) {
	uint8_t mask = 1 << btn;
	if (down) {
		device->buttons[controller_index] |= mask;
	} else {
		device->buttons[controller_index] &= ~mask;
	}
}

static inline void
buxn_controller_send_button(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	int controller_index,
	buxn_controller_btn_t btn,
	bool down
) {
	buxn_controller_set_button(device, controller_index, btn, down);
	buxn_controller_send_event(vm);
}

static inline void
buxn_controller_send_char(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	char ch
) {
	device->ch = ch;
	buxn_controller_send_event(vm);
	device->ch = 0;
}

#endif
