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

	basm->suppress_report = true;
	BTEST_EXPECT(!buxn_asm_str(basm, ""));
	BTEST_EXPECT(!buxn_asm_str(basm, "BRK @Routine ( -- )"));
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

BTEST(chess, jump) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Constant jump
	BTEST_EXPECT(buxn_asm_str(basm, "#01 #02 GTHk JMP SWP POP POP BRK"));
	BTEST_EXPECT(basm->num_warnings == 0);

	// Do not get stuck in loop
	basm->suppress_report = true;
	BTEST_EXPECT(!buxn_asm_str(basm, "&>l !/>l"));
	BTEST_EXPECT(basm->num_errors == 1);
	basm->suppress_report = false;

	// Recursion
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@print-string ( [str]* -- )\n"
			" LDAk DUP ?{ POP POP2 JMP2r }\n"
			" POP\n"
			" INC2 !print-string"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);

	// Unknown jump
	basm->suppress_report = true;
	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@Unknown ( [str]* -- )\n"
			" JMP2\n"
		)
	);
	BTEST_EXPECT(basm->num_errors == 1);
	basm->suppress_report = false;

	// Branch fail
	basm->suppress_report = true;

	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@branching ( [a]* -- c )\n"
			"LDAk #01 EQU ?&one\n"
			"LDAk #02 EQU ?&two\n"
			"POP2 #ff JMP2r\n"
			"&one POP2 JMP2r\n" // Fail here
			"&two ADD JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_errors == 1);

	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@branching ( [a]* -- c )\n"
			"LDAk #01 EQU ?&one\n"
			"LDAk #02 EQU ?&two\n"
			"POP2 JMP2r\n" // Fail here
			"&one POP2 #ff JMP2r\n"
			"&two ADD JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_errors == 1);

	basm->suppress_report = false;
}

BTEST(chess, merge_value) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Merge a broken short back and automatically restore type info
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@Store ( [addr]* value -- )\n"
			"ROT ROT STA JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);

	// DUP should no longer break type info
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@Store ( value [addr]* -- )\n"
			"DUP POP STA JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);

	// Halves join back
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@Store ( [addr]* -- )\n"
			"SWP OVR STA JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);
}

BTEST(chess, termination) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	basm->suppress_report = true;
	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@loop ( cond -- )\n"
			"!loop JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_errors == 1);
	basm->suppress_report = false;

	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n" // End reset
			"@loop ( cond -- )\n"
			"DUP #01 SUB ?loop POP JMP2r\n"
		)
	);
}

BTEST(chess, trusted_signature) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n"
			"@Trusted ( a b -- ! )\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);
}

BTEST(chess, cast) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n"
			"@Store ( a [addr]* -- ) INC ( [addr]* ! ) STA JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);

	// Cast declares more elements than available
	basm->suppress_report = true;
	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"BRK\n"
			"@Store ( a [addr]* -- ) INC ( [addr]* [addr]* ! ) STA JMP2r\n"
		)
	);
	basm->suppress_report = false;

	// Cast using macro
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"BRK\n"
			"%>ADDR { ( [addr]* ! ) }\n"
			"@Store ( a [addr]* -- ) INC >ADDR STA JMP2r\n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);
}

BTEST(chess, macro) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Macro with annotation must not attach to the preceding label
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"|00 @Device\n"
			"%Macro ( -- ) {  }\n"
			"|0100 @on-reset ( -> ) BRK \n"
		)
	);
	BTEST_EXPECT(basm->num_warnings == 0);
}
