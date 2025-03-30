#ifndef BUXN_DEVICE_SCREEN_H
#define BUXN_DEVICE_SCREEN_H

// Adapted from: https://git.sr.ht/~rabbits/uxn/tree/main/item/src/devices/screen.h
/*
Copyright (c) 2021-2025 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct buxn_vm_s;

typedef struct UxnScreen {
	int rX, rY, rA, rMX, rMY, rMA, rML, rDX, rDY;
	int width, height, vector, x1, y1, x2, y2;
	uint8_t* fg;
	uint8_t bg[];
} buxn_screen_t;

typedef enum {
	BUXN_SCREEN_LAYER_BACKGROUND,
	BUXN_SCREEN_LAYER_FOREGROUND,
} buxn_screen_layer_type_t;

typedef struct {
	size_t screen_mem_size;
	size_t target_mem_size;
} buxn_screen_info_t;

buxn_screen_info_t
buxn_screen_info(uint16_t width, uint16_t height);

void
buxn_screen_resize(buxn_screen_t* screen, uint16_t width, uint16_t height);

void
buxn_screen_requested_size(struct buxn_vm_s* vm, uint16_t* width, uint16_t* height);

void
buxn_screen_update(struct buxn_vm_s* vm);

bool
buxn_screen_render(
	buxn_screen_t* screen,
	buxn_screen_layer_type_t layer,
	uint32_t palette[4],
	uint32_t* target
);

uint8_t
buxn_screen_dei(struct buxn_vm_s* vm, buxn_screen_t* device, uint8_t address);

void
buxn_screen_deo(struct buxn_vm_s* vm, buxn_screen_t* device, uint8_t address);

#endif
