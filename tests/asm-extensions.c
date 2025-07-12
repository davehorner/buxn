#include <btest.h>
#include "common.h"
#include <buxn/vm/vm.h>
#include <buxn//devices/system.h>
#include <string.h>

static struct {
	barena_pool_t pool;
	barena_t arena;
	buxn_asm_ctx_t basm;
	buxn_test_devices_t devices;
	buxn_vm_t* vm;
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
	};

	memset(&fixture.devices, 0, sizeof(fixture.devices));
	fixture.vm = barena_malloc(
		&fixture.arena,
		sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE
	);
	fixture.vm->config = (buxn_vm_config_t){
		.memory_size = BUXN_MEMORY_BANK_SIZE,
		.userdata = &fixture.devices,
	};
}

static void
cleanup_per_test(void) {
	barena_reset(&fixture.arena);
}

static btest_suite_t basm_ext = {
	.name = "asm/extensions",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

static int
basm_ext_execute_rom(void) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	buxn_vm_t* vm = fixture.vm;

	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);
	buxn_console_init(fixture.vm, &fixture.devices.console, 0, NULL);
	memcpy(vm->memory + BUXN_RESET_VECTOR, basm->rom, basm->rom_size);
	buxn_vm_execute(vm, BUXN_RESET_VECTOR);
	return buxn_system_exit_code(vm);
}

BTEST(basm_ext, decimal) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Padding with decimal
	BTEST_EXPECT(buxn_asm_str(basm, "|00 |++256 BRK"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 1);

	BTEST_EXPECT(buxn_asm_str(basm, "|00 $++256 BRK"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 1);

	BTEST_EXPECT(buxn_asm_str(basm, "|00 $+255 $+1 BRK"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 1);

	// Number size
	BTEST_EXPECT(buxn_asm_str(basm, "+128"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 1);

	BTEST_EXPECT(buxn_asm_str(basm, "++128"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 2);

	// Decimal literal
	BTEST_EXPECT(buxn_asm_str(basm, "|00 |++256 #+11 #0b EQU #0f DEO BRK"));
	int exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 1);

	BTEST_EXPECT(buxn_asm_str(basm, "|00 |++256 #++1234 #04d2 EQU2 #0f DEO BRK"));
	exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 1);

	// Push 1 byte to the stack
	BTEST_EXPECT(buxn_asm_str(basm, "|00 |++256 #+234 #04 DEI #01 EQU #0f DEO BRK"));
	exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 1);

	// Push 2 bytes to the stack
	BTEST_EXPECT(buxn_asm_str(basm, "|00 |++256 #++1234 #04 DEI #02 EQU #0f DEO BRK"));
	exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 1);

	// Errors
	basm->suppress_report = true;
	BTEST_EXPECT(!buxn_asm_str(basm, "+279"));  // Too big
	BTEST_EXPECT(!buxn_asm_str(basm, "++999999999"));  // Too big
	BTEST_EXPECT(!buxn_asm_str(basm, "#+2b"));  // Invalid char
	BTEST_EXPECT(!buxn_asm_str(basm, "#++2b"));  // Invalid char
	basm->suppress_report = false;
}

BTEST(basm_ext, at_label) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Save/load padding
	BTEST_EXPECT(buxn_asm_str(basm, "@@ |00 |@ POP BRK"));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, 2);

	// Loop
	BTEST_EXPECT(buxn_asm_str(basm, "#00 @@ INCk #08 NEQ ?@ POP BRK"));

	// Unreferenced
	basm->suppress_report = true;
	BTEST_EXPECT(buxn_asm_str(basm, "@@ @@"));
	BTEST_EXPECT_EQUAL("%d", basm->num_warnings, 2);

	// Single use
	BTEST_EXPECT(!buxn_asm_str(basm, "#00 @@ INCk #08 NEQ ?@ POP !@ BRK"));
	basm->suppress_report = false;
}

BTEST(basm_ext, long_string) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(buxn_asm_str(basm, "\" long  string is long my dude can you imagine how long it is?\""));
	BTEST_EXPECT_EQUAL("%d", basm->rom_size, sizeof("long  string is long my dude can you imagine how long it is?") - 1);

	// Unterminated
	basm->suppress_report = true;
	BTEST_EXPECT(!buxn_asm_str(basm, "\" "));
	basm->suppress_report = false;
}

BTEST(basm_ext, macro_with_arg) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	int exit_code;

	// Simple expansion
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"%Macro-with-arg: { #^ }\n"
			"Macro-with-arg: 02 #0f DEO BRK"
		)
	);
	exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 2);

	// Chained expansion
	BTEST_EXPECT(
		buxn_asm_str(
			basm,
			"%Macro1: { Macro^: 02 }\n"
			"%Macro2: { #^ }\n"
			"Macro1: 2 #0f DEO BRK"
		)
	);
	exit_code = basm_ext_execute_rom();
	BTEST_EXPECT_EQUAL("%d", exit_code, 2);

	// Error
	basm->suppress_report = true;
	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"%Macro: { ^ }\n"
			"Macro:"
		)
	);
	BTEST_EXPECT(
		!buxn_asm_str(
			basm,
			"%Macro: { a-^ }\n"
			"Macro: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		)
	);
	basm->suppress_report = false;
}
