#include <btest.h>
#include <string.h>
#include "common.h"
#include "resources.h"

static struct {
	barena_pool_t pool;
	buxn_asm_ctx_t basm;
} fixture;

static buxn_vfs_entry_t empty_vfs[] = {
	{ 0 },
};

static void
init(void) {
	barena_pool_init(&fixture.pool, 1);
	barena_init(&fixture.basm.arena, &fixture.pool);
	fixture.basm.rom_size = 0;
	fixture.basm.vfs = empty_vfs;
}

static void
cleanup(void) {
	barena_reset(&fixture.basm.arena);
	barena_pool_cleanup(&fixture.pool);
}

static btest_suite_t basm = {
	.name = "asm",
	.init = init,
	.cleanup = cleanup,
};

static inline bool
buxn_asm_str(buxn_asm_ctx_t* basm, const char* str) {
	basm->vfs = (buxn_vfs_entry_t[]) {
		{
			.name = "<inline>",
			.content = { .data = (const unsigned char*)str, .size = strlen(str) }
		},
		{ 0 },
	};
	basm->rom_size = 0;

	barena_snapshot_t snapshot = barena_snapshot(&basm->arena);
	bool result = buxn_asm(basm, "<inline>");
	barena_restore(&basm->arena, snapshot);

	return result;
}

BTEST(basm, acid) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->vfs = (buxn_vfs_entry_t[]) {
		{ .name = "acid.tal", .content = XINCBIN_GET(acid_tal) },
		{ 0 },
	};

	BTEST_ASSERT(buxn_asm(basm, "acid.tal"));
	// TODO: execute the ROM
	// TODO: check against drifblim
}

BTEST(basm, empty) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	BTEST_ASSERT(buxn_asm_str(basm, "@scope"));
	BTEST_ASSERT(basm->rom_size == 0);
}

BTEST(basm, token) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope ; @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope . @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope , @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope LIT2 = @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope LIT - @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope LIT _ @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope LIT | @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope \" @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope ! @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope ? @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope # @end"));
	BTEST_ASSERT(!buxn_asm_str(basm, "@scope AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA @end"));
}
