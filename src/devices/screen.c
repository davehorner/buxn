#include <string.h>
#include "screen.h"
#include "../vm.h"

// Adapted from: https://git.sr.ht/~rabbits/uxn/tree/main/item/src/devices/screen.h
/*
Copyright (c) 2021-2025 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define MAR(x)  (x + 0x8)
#define MAR2(x) (x + 0x10)
#define clamp(v,a,b) { if(v < a) v = a; else if(v >= b) v = b; }
#define twos(v) (v & 0x8000 ? (int)v - 0x10000 : (int)v)

static const uint8_t BUXN_BLENDING[4][16] = {
	{0, 0, 0, 0, 1, 0, 1, 1, 2, 2, 0, 2, 3, 3, 3, 0},
	{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
	{1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1},
	{2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2}
};

static void
buxn_screen_dirty(
	buxn_screen_rect_t* rect,
	int x1, int y1, int x2, int y2
) {
	if(x1 < rect->x1) rect->x1 = x1;
	if(y1 < rect->y1) rect->y1 = y1;
	if(x2 > rect->x2) rect->x2 = x2;
	if(y2 > rect->y2) rect->y2 = y2;
}

buxn_screen_info_t
buxn_screen_info(uint16_t width, uint16_t height) {
	size_t length = MAR2(width) * MAR2(height);
	return (buxn_screen_info_t){
		.screen_mem_size = sizeof(buxn_screen_t) + length * 2,
		.target_mem_size = width * height * sizeof(uint32_t),
	};
}

void
buxn_screen_resize(buxn_screen_t* screen, uint16_t width, uint16_t height) {
	size_t length = MAR2(width) * MAR2(height);
	screen->fg = screen->bg + length;
	screen->width = width;
	screen->height = height;
	memset(screen->bg, 0, length * 2);

	buxn_screen_dirty(&screen->bg_dirty_rect, 0, 0, width, height);
	buxn_screen_dirty(&screen->fg_dirty_rect, 0, 0, width, height);
}

void
buxn_screen_preferred_size(struct buxn_vm_s* vm, uint16_t* width, uint16_t* height) {
	*width  = buxn_vm_dev_load2(vm, 0x22);
	*height = buxn_vm_dev_load2(vm, 0x24);
}

bool
buxn_screen_render(
	buxn_screen_t* device,
	buxn_screen_layer_type_t layer,
	uint32_t palette[4],
	uint32_t* target
) {
	const uint8_t* rendered_layer = layer == BUXN_SCREEN_LAYER_BACKGROUND ? &device->bg[0] : &device->fg[0];
	buxn_screen_rect_t* rect = layer == BUXN_SCREEN_LAYER_BACKGROUND ? &device->bg_dirty_rect : &device->fg_dirty_rect;

	clamp(rect->x1, 0, device->width);
	clamp(rect->x2, 0, device->width);
	clamp(rect->y1, 0, device->height);
	clamp(rect->y2, 0, device->height);

	if (!(rect->x2 > rect->x1 && rect->y2 > rect->y1)) {
		return false;
	}

	int i, x, y;
	for(y = rect->y1; y < rect->y2; y++) {
		for(x = rect->x1, i = MAR(x) + MAR(y) * MAR2(device->width); x < rect->x2; x++, i++) {
			int c = palette[rendered_layer[i]];
			int oo = (y * device->width + x);
			target[oo] = c;
		}
	}
	rect->x1 = device->width + 1;
	rect->y1 = device->height + 1;
	rect->x2 = rect->y2 = 0;

	return true;
}

uint8_t
buxn_screen_dei(struct buxn_vm_s* vm, buxn_screen_t* device, uint8_t address) {
	switch (address) {
		case 0x22: return device->width >> 8;
		case 0x23: return device->width;
		case 0x24: return device->height >> 8;
		case 0x25: return device->height;
		case 0x28: return device->rX >> 8;
		case 0x29: return device->rX;
		case 0x2a: return device->rY >> 8;
		case 0x2b: return device->rY;
		case 0x2c: return device->rA >> 8;
		case 0x2d: return device->rA;
		default: return vm->device[address];
	}
}

static buxn_screen_t*
buxn_screen_maybe_resize(struct buxn_vm_s* vm, buxn_screen_t* device) {
	uint16_t preferred_width, preferred_height;
	buxn_screen_preferred_size(vm, &preferred_width, &preferred_height);
	if (preferred_width != device->width || preferred_height != device->height) {
		return buxn_screen_request_resize(vm, device, preferred_width, preferred_height);
	} else {
		return device;
	}
}

void
buxn_screen_deo(struct buxn_vm_s* vm, buxn_screen_t* device, uint8_t address) {
	switch(address) {
		case 0x26: {
			device->rMX = vm->device[0x26] & 0x1;
			device->rMY = vm->device[0x26] & 0x2;
			device->rMA = vm->device[0x26] & 0x4;
			device->rML = vm->device[0x26] >> 4;
			device->rDX = device->rMX << 3;
			device->rDY = device->rMY << 2;
		} break;
		case 0x28:
		case 0x29: {
			device->rX = (vm->device[0x28] << 8) | vm->device[0x29];
			device->rX = twos(device->rX);
		} break;
		case 0x2a:
		case 0x2b: {
			device->rY = (vm->device[0x2a] << 8) | vm->device[0x2b];
			device->rY = twos(device->rY);
		} break;
		case 0x2c:
		case 0x2d: {
			device->rA = (vm->device[0x2c] << 8) | vm->device[0x2d];
		} break;
		case 0x2e: {
			device = buxn_screen_maybe_resize(vm, device);
			int ctrl = vm->device[0x2e];
			int color = ctrl & 0x3;
			int len = MAR2(device->width);
			uint8_t* layer = ctrl & 0x40 ? device->fg : device->bg;
			buxn_screen_rect_t* rect = ctrl & 0x40 ? &device->fg_dirty_rect : &device->bg_dirty_rect;
			if(ctrl & 0x80) {
				/* fill mode */
				int x1, y1, x2, y2, ax, bx, ay, by, hor, ver;
				if(ctrl & 0x10) {
					x1 = MAR(0), x2 = MAR(device->rX);
				} else {
					x1 = MAR(device->rX), x2 = MAR(device->width);
				}
				if(ctrl & 0x20) {
					y1 = MAR(0), y2 = MAR(device->rY);
				} else {
					y1 = MAR(device->rY), y2 = MAR(device->height);
				}
				hor = x2 - x1, ver = y2 - y1;
				for(ay = y1 * len, by = ay + ver * len; ay < by; ay += len) {
					for(ax = ay + x1, bx = ax + hor; ax < bx; ax++) {
						layer[ax] = color;
					}
				}
				buxn_screen_dirty(rect, x1, y1, x2, y2);
			} else {
				/* pixel mode */
				if(device->rX >= 0 && device->rY >= 0 && device->rX < len && device->rY < device->height) {
					layer[MAR(device->rX) + MAR(device->rY) * len] = color;
				}
				buxn_screen_dirty(rect, device->rX, device->rY, device->rX + 1, device->rY + 1);
				if(device->rMX) device->rX++;
				if(device->rMY) device->rY++;
			}
		} break;
		case 0x2f: {
			device = buxn_screen_maybe_resize(vm, device);
			int ctrl = vm->device[0x2f];
			int blend = ctrl & 0xf, opaque = blend % 5;
			int fx = ctrl & 0x10 ? -1 : 1, fy = ctrl & 0x20 ? -1 : 1;
			int qfx = fx > 0 ? 7 : 0, qfy = fy < 0 ? 7 : 0;
			int dxy = fy * device->rDX, dyx = fx * device->rDY;
			int wmar = MAR(device->width), wmar2 = MAR2(device->width);
			int hmar2 = MAR2(device->height);
			int i, x1, x2, y1, y2, ax, ay, qx, qy, x = device->rX, y = device->rY;
			uint8_t* layer = ctrl & 0x40 ? device->fg : device->bg;
			buxn_screen_rect_t* rect = ctrl & 0x40 ? &device->fg_dirty_rect : &device->bg_dirty_rect;
			if(ctrl & 0x80) {
				int addr_incr = device->rMA << 2;
				for(i = 0; i <= device->rML; i++, x += dyx, y += dxy, device->rA += addr_incr) {
					uint16_t xmar = MAR(x), ymar = MAR(y);
					uint16_t xmar2 = MAR2(x), ymar2 = MAR2(y);
					if(xmar < wmar && ymar2 < hmar2) {
						uint8_t *sprite = &vm->memory[device->rA];
						int by = ymar2 * wmar2;
						for(ay = ymar * wmar2, qy = qfy; ay < by; ay += wmar2, qy += fy) {
							int ch1 = sprite[qy], ch2 = sprite[qy + 8] << 1, bx = xmar2 + ay;
							for(ax = xmar + ay, qx = qfx; ax < bx; ax++, qx -= fx) {
								int color = ((ch1 >> qx) & 1) | ((ch2 >> qx) & 2);
								if(opaque || color) {
									layer[ax] = BUXN_BLENDING[color][blend];
								}
							}
						}
					}
				}
			} else {
				int addr_incr = device->rMA << 1;
				for(i = 0; i <= device->rML; i++, x += dyx, y += dxy, device->rA += addr_incr) {
					uint16_t xmar = MAR(x), ymar = MAR(y);
					uint16_t xmar2 = MAR2(x), ymar2 = MAR2(y);
					if(xmar < wmar && ymar2 < hmar2) {
						uint8_t *sprite = &vm->memory[device->rA];
						int by = ymar2 * wmar2;
						for(ay = ymar * wmar2, qy = qfy; ay < by; ay += wmar2, qy += fy) {
							int ch1 = sprite[qy], bx = xmar2 + ay;
							for(ax = xmar + ay, qx = qfx; ax < bx; ax++, qx -= fx) {
								int color = (ch1 >> qx) & 1;
								if(opaque || color) {
									layer[ax] = BUXN_BLENDING[color][blend];
								}
							}
						}
					}
				}
			}
			if(fx < 0) {
				x1 = x, x2 = device->rX;
			} else {
				x1 = device->rX, x2 = x;
			}
			if(fy < 0) {
				y1 = y, y2 = device->rY;
			} else {
				y1 = device->rY, y2 = y;
			}
			buxn_screen_dirty(rect, x1 - 8, y1 - 8, x2 + 8, y2 + 8);
			if(device->rMX) device->rX += device->rDX * fx;
			if(device->rMY) device->rY += device->rDY * fy;
		} break;
	}
}

void
buxn_screen_update(struct buxn_vm_s* vm) {
	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x20);
	if (vector_addr != 0) { buxn_vm_execute(vm, vector_addr); }
}
