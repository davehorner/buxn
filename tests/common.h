#ifndef BUXN_TEST_COMMON_H
#define BUXN_TEST_COMMON_H

#include <xincbin.h>
#include <barena.h>
#include "../src/asm/asm.h"
#include "../src/devices/console.h"
#include "../src/devices/mouse.h"

typedef struct {
	const char* name;
	xincbin_data_t content;
} buxn_vfs_entry_t;

struct buxn_asm_ctx_s {
	buxn_vfs_entry_t* vfs;
	barena_t* arena;

	bool suppress_report;
	char rom[UINT16_MAX];
	uint16_t rom_size;

	int num_errors;
	int num_warnings;
};

typedef struct {
	buxn_console_t console;
	buxn_mouse_t mouse;
} buxn_test_devices_t;

#endif
