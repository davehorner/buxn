#ifndef BUXN_TEST_COMMON_H
#define BUXN_TEST_COMMON_H

#include <xincbin.h>
#include <barena.h>
#include <buxn/asm/asm.h>
#include <buxn/asm/chess.h>
#include <buxn/devices/console.h>
#include <buxn/devices/mouse.h>

typedef struct {
	const char* name;
	xincbin_data_t content;
} buxn_vfs_entry_t;

struct buxn_asm_ctx_s {
	buxn_vfs_entry_t* vfs;
	barena_t* arena;
	buxn_chess_t* chess;
	bool enable_chess;

	bool suppress_report;
	char rom[UINT16_MAX];
	uint16_t rom_size;

	int num_errors;
	int num_warnings;
};

typedef struct {
	buxn_console_t console;
	buxn_mouse_t mouse;

	void (*system_dbg)(struct buxn_vm_s* vm, uint8_t value);
} buxn_test_devices_t;

bool
buxn_asm_str(buxn_asm_ctx_t* basm, const char* str, const char* file, int line);

#define buxn_asm_str(basm, str) buxn_asm_str(basm, str, __FILE__, __LINE__)

#endif
