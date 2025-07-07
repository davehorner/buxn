#include <btest.h>
#include "common.h"

static struct {
	barena_pool_t pool;
	barena_t arena;
	buxn_asm_ctx_t basm;
} fixture;

static buxn_vfs_entry_t empty_vfs[] = {
	{ 0 },
};

static void
init_per_suite(void) {
	barena_pool_init(&fixture.pool, 1);
}

static void
cleanup_per_suite(void) {
	barena_pool_cleanup(&fixture.pool);
}

static void
init_per_test(void) {
	barena_init(&fixture.arena, &fixture.pool);

	fixture.basm = (buxn_asm_ctx_t){
		.arena = &fixture.arena,
		.vfs = empty_vfs,
		.enable_chess = true,
	};
}

static void
cleanup_per_test(void) {
	barena_reset(&fixture.arena);
}

static btest_suite_t chess = {
	.name = "chess",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(chess, empty) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(buxn_asm_str(basm, ""));
	BTEST_EXPECT(basm->num_warnings == 0);
}

BTEST(chess, lit) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(buxn_asm_str(basm, "LIT 02 POP BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT &test $1 POP BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 &test $2 POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 &test $1 \"a POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 01 \"a POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 01 02 POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 \"a 02 POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	BTEST_EXPECT(buxn_asm_str(basm, "LIT2 \"ab POP2 BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);
}
