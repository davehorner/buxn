#ifndef BUXN_DEVICE_CONTROLLER_H
#define BUXN_DEVICE_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#define BUXN_NUM_CONTROLLERS 4

struct buxn_vm_s;

typedef struct {
	uint8_t buttons[4];
	uint8_t key;
} buxn_controller_t;

typedef enum {
	BUXN_CONTROLLER_KEY_A,
	BUXN_CONTROLLER_KEY_B,
	BUXN_CONTROLLER_KEY_SELECT,
	BUXN_CONTROLLER_KEY_START,
	BUXN_CONTROLLER_KEY_UP,
	BUXN_CONTROLLER_KEY_DOWN,
	BUXN_CONTROLLER_KEY_LEFT,
	BUXN_CONTROLLER_KEY_RIGHT,
} buxn_controller_key_t;

uint8_t
buxn_controller_dei(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address);

void
buxn_controller_deo(struct buxn_vm_s* vm, buxn_controller_t* device, uint8_t address);

void
buxn_controller_set_button(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	int controller_index,
	buxn_controller_key_t key,
	bool down
);

void
buxn_controller_send_key(
	struct buxn_vm_s* vm,
	buxn_controller_t* device,
	char key
);

#endif
