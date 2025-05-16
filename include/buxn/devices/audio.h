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

#define BUXN_AUDIO_PREFERRED_SAMPLE_RATE 44100
#define BUXN_AUDIO_PREFERRED_NUM_CHANNELS 2

struct buxn_vm_s;
struct buxn_audio_s;

typedef struct {
	struct buxn_audio_s* device;

	uint8_t* addr;
	uint16_t adsr;
	uint16_t len;
	uint8_t pitch;
	uint8_t repeat;
	int8_t volume[2];
} buxn_audio_message_t;

typedef struct buxn_audio_s {
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

void
buxn_audio_notify_finished(struct buxn_vm_s* vm, uint8_t device_id);

buxn_audio_state_t
buxn_audio_render(buxn_audio_t* device, float* stream, int len, int num_channels);

void
buxn_audio_receive(const buxn_audio_message_t* message);

// Must be provided by the host program

extern void
buxn_audio_send(struct buxn_vm_s* vm, const buxn_audio_message_t* message);

#endif
