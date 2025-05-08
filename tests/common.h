#ifndef BUXN_TEST_COMMON_H
#define BUXN_TEST_COMMON_H

#include "../src/asm/asm.h"
#include <xincbin.h>
#include <barena.h>

typedef struct {
	const char* name;
	xincbin_data_t content;
} buxn_vfs_entry_t;

struct buxn_asm_ctx_s {
	buxn_vfs_entry_t* vfs;
	barena_t arena;
	bool suppress_report;
	char rom[UINT16_MAX];
	uint16_t rom_size;
};

#endif
