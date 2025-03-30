#ifndef BUXN_DEVICE_AUDIO_H
#define BUXN_DEVICE_AUDIO_H

// Adapted from: https://git.sr.ht/~rabbits/uxn/tree/main/item/src/devices/audio.h
/*
Copyright (c) 2021-2025 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#include <stdint.h>

struct buxn_vm_s;

typedef struct {
	uint32_t sample_frequency;

	uint8_t* addr;
	uint32_t count, advance, period, age, a, d, s, r;
	uint16_t i, len;
	int8_t volume[2];
	uint8_t pitch, repeat;
} buxn_audio_t;

typedef enum {
	BUXN_AUDIO_STOPPED,
	BUXN_AUDIO_PLAYING,
	BUXN_AUDIO_FINISHED,
} buxn_audio_state_t;

uint8_t
buxn_audio_dei(struct buxn_vm_s* vm, buxn_audio_t* device, uint8_t* mem, uint8_t port);

void
buxn_audio_deo(struct buxn_vm_s* vm, buxn_audio_t* device, uint8_t* mem, uint8_t port);

buxn_audio_state_t
buxn_audio_get_samples(buxn_audio_t* device, float* stream, int len);

// Must be provided by the host program
extern void
buxn_audio_lock_device(void);

extern void
buxn_audio_unlock_device(void);

#endif
