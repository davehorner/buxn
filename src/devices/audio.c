#include "audio.h"
#include "../vm.h"

// Adapted from: https://git.sr.ht/~rabbits/uxn/tree/main/item/src/devices/audio.h
/*
Copyright (c) 2021-2025 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

static const uint32_t BUXN_AUDIO_ADVANCES[12] = {
	0x80000, 0x879c8, 0x8facd, 0x9837f, 0xa1451, 0xaadc1,
	0xb504f, 0xbfc88, 0xcb2ff, 0xd7450, 0xe411f, 0xf1a1c
};

static int32_t
buxn_audio_envelope(buxn_audio_t* c, uint32_t age) {
	if(!c->r) return 0x0888;
	if(age < c->a) return 0x0888 * age / c->a;
	if(age < c->d) return 0x0444 * (2 * c->d - c->a - age) / (c->d - c->a);
	if(age < c->s) return 0x0444;
	if(age < c->r) return 0x0444 * (c->r - age) / (c->r - c->s);
	c->advance = 0;
	return 0x0000;
}

static uint8_t
buxn_audio_get_vu(buxn_audio_t* device) {
	int32_t sum[2] = { 0, 0 };
	if(!device->advance || !device->period) { return 0; }
	for(int i = 0; i < 2; i++) {
		if(!device->volume[i]) { continue; }
		sum[i] = 1 + buxn_audio_envelope(device,  device->age) * device->volume[i] / 0x800;
		if(sum[i] > 0xf) sum[i] = 0xf;
	}
	return (sum[0] << 4) | sum[1];
}

static uint16_t
buxn_audio_load2(uint8_t* mem) {
	uint16_t hi = (uint16_t)mem[0] << 8;
	uint16_t lo = (uint16_t)mem[1];
	return hi | lo;
}

void
buxn_audio_receive(const buxn_audio_message_t* message) {
	buxn_audio_t* c = message->device;
	const uint32_t note_period = c->sample_frequency * 0x4000 / 11025;
	const uint32_t adsr_step = c->sample_frequency / 0xf;

	uint8_t pitch = message->pitch;
	uint16_t adsr = message->adsr;
	c->len = message->len;
	c->addr = message->addr;
	c->volume[0] = message->volume[0];
	c->volume[1] = message->volume[1];
	c->repeat = message->repeat;
	if(pitch < 108 && c->len)
		c->advance = BUXN_AUDIO_ADVANCES[pitch % 12] >> (8 - pitch / 12);
	else {
		c->advance = 0;
		return;
	}
	c->a = adsr_step * (adsr >> 12);
	c->d = adsr_step * (adsr >> 8 & 0xf) + c->a;
	c->s = adsr_step * (adsr >> 4 & 0xf) + c->d;
	c->r = adsr_step * (adsr >> 0 & 0xf) + c->s;
	c->age = 0;
	c->i = 0;
	if(c->len <= 0x100) {
		/* single cycle mode */
		c->period = note_period * 337 / 2 / c->len;
	} else {
		/* sample repeat mode */
		c->period = note_period;
	}
}

uint8_t
buxn_audio_dei(struct buxn_vm_s* vm, buxn_audio_t* device, uint8_t* mem, uint8_t port) {
	(void)vm;
	switch(port) {
		case 0x4: return buxn_audio_get_vu(device);
		case 0x2: {
			uint16_t position = device->i;
			mem[0x2] = (uint8_t)(position >> 8);
			mem[0x3] = (uint8_t)(position & 0xff);
		};
		default: return mem[port];
	}
}

void
buxn_audio_deo(struct buxn_vm_s* vm, buxn_audio_t* device, uint8_t* mem, uint8_t port) {
	if(port == 0xf) {
		uint8_t pitch = mem[0xf] & 0x7f;
		uint16_t addr = buxn_audio_load2(mem + 0xc);
		uint16_t adsr = buxn_audio_load2(mem + 0x8);
		uint16_t len = buxn_audio_load2(mem + 0xa);
		if(len > 0x10000 - addr)
			len = 0x10000 - addr;
		uint8_t* sample_ptr = &vm->memory[addr];
		int8_t volume_0 = mem[0xe] >> 4;
		int8_t volume_1 = mem[0xe] & 0xf;
		uint8_t repeat = !(mem[0xf] & 0x80);

		buxn_audio_send(vm, &(buxn_audio_message_t){
			.device = device,
			.addr = sample_ptr,
			.adsr = adsr,
			.len = len,
			.pitch = pitch,
			.repeat = repeat,
			.volume = { volume_0, volume_1 },
		});
	}
}

buxn_audio_state_t
buxn_audio_render(buxn_audio_t* c, float* stream, int len) {
	int32_t s;
	float* sample = stream;
	float* end = sample + len * 2;  // TODO: handle mono
	if(!c->advance || !c->period) { return BUXN_AUDIO_STOPPED; }

	while(sample < end) {
		c->count += c->advance;
		c->i += c->count / c->period;
		c->count %= c->period;
		if(c->i >= c->len) {
			if(!c->repeat) {
				c->advance = 0;
				break;
			}
			c->i %= c->len;
		}
		s = (int8_t)(c->addr[c->i] + 0x80) * buxn_audio_envelope(c, c->age++);
		// TODO: handle single channel audio
		*sample++ += (float)(s * c->volume[0]) / (float)0x180 / (float)(-INT16_MIN);
		*sample++ += (float)(s * c->volume[1]) / (float)0x180 / (float)(-INT16_MIN);
	}

	return !c->advance ? BUXN_AUDIO_FINISHED : BUXN_AUDIO_PLAYING;
}
