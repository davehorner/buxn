#include <btest.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"
#include <buxn/vm/vm.h>
#include <buxn//devices/system.h>
#include "resources.h"

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
	};
}

static void
cleanup_per_test(void) {
	barena_reset(&fixture.arena);
}

static btest_suite_t basm = {
	.name = "asm",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

static inline bool
buxn_asm_str(buxn_asm_ctx_t* basm, const char* str, const char* file, int line) {
	barena_snapshot_t snapshot = barena_snapshot(basm->arena);

	int size = snprintf(NULL, 0, "%s:%d", file, line);
	char* filename = barena_malloc(basm->arena, size + 1);
	snprintf(filename, size + 1, "%s:%d", file, line);

	basm->vfs = (buxn_vfs_entry_t[]) {
		{
			.name = filename,
			.content = { .data = (const unsigned char*)str, .size = strlen(str) }
		},
		{ 0 },
	};
	basm->rom_size = 0;
	basm->num_errors = 0;
	basm->num_warnings = 0;

	bool result = buxn_asm(basm, filename);

	barena_restore(basm->arena, snapshot);

	return result;
}

#define buxn_asm_str(basm, str) buxn_asm_str(basm, str, __FILE__, __LINE__)

BTEST(basm, warning) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	// Unused label
	BTEST_EXPECT(buxn_asm_str(basm, "|00 @scope"));
	BTEST_EXPECT(basm->num_warnings == 1);

	// Unused label starting with capital letter
	BTEST_EXPECT(buxn_asm_str(basm, "|00 @Main"));
	BTEST_EXPECT(basm->num_warnings == 0);

	// Reset vector label
	BTEST_EXPECT(buxn_asm_str(basm, "|0100 @on-reset"));
	BTEST_EXPECT(basm->num_warnings == 0);

	// Redundant flag
	BTEST_EXPECT(buxn_asm_str(basm, "EQU2222"));
	BTEST_EXPECT(basm->num_warnings == 1);

	BTEST_EXPECT(buxn_asm_str(basm, "LITk"));
	BTEST_EXPECT(basm->num_warnings == 1);

	// Label used for padding
	BTEST_EXPECT(buxn_asm_str(basm, "|01 @here |00 |here"));
	BTEST_EXPECT(basm->num_warnings == 0);
}

// Ported from: https://git.sr.ht/~rabbits/drifblim/commit/d8aba81f35f3f398ceeb653756a9597531e600cf

BTEST(basm, acid) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->vfs = (buxn_vfs_entry_t[]) {
		{ .name = "acid.tal", .content = XINCBIN_GET(acid_tal) },
		{ 0 },
	};

	BTEST_ASSERT(buxn_asm(basm, "acid.tal"));

	// Load and execute rom
	buxn_test_devices_t devices = { 0 };
	buxn_vm_t* vm = barena_malloc(
		&fixture.arena,
		sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE
	);
	vm->config = (buxn_vm_config_t){
		.memory_size = BUXN_MEMORY_BANK_SIZE,
		.userdata = &devices,
	};
	buxn_vm_reset(vm, BUXN_VM_RESET_ALL);
	buxn_console_init(vm, &devices.console, 0, NULL);
	memcpy(vm->memory + BUXN_RESET_VECTOR, basm->rom, basm->rom_size);
	buxn_vm_execute(vm, BUXN_RESET_VECTOR);
	BTEST_ASSERT(buxn_system_exit_code(vm) < 0);

	// TODO: check against drifblim's output
}

BTEST(basm, empty) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	BTEST_ASSERT(buxn_asm_str(basm, "@scope"));
	BTEST_ASSERT(basm->rom_size == 0);
}

BTEST(basm, token) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ; @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope . @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope , @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT2 = @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT - @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT _ @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT | @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope \" @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ! @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ? @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope # @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA @end @AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
}

BTEST(basm, comment) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ( BRK @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope #01 (BRK ) @end"));
}

BTEST(basm, writing) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope |80 #1234 @end"));
}

BTEST(basm, symbol) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope @foo @foo @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope @1234 @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope @-1234 @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope @LDA @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "%label { SUB } @label"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope &foo &foo @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@AAAAAAAAAAAAAAAAAAAAAAAAA &BBBBBBBBBBBBBBBBBBBBBBB @end"));
}

BTEST(basm, opcode) {
	buxn_asm_ctx_t* basm = &fixture.basm;

	BTEST_EXPECT(buxn_asm_str(basm, "@scope ADD2q @end @ADD2q"));
	BTEST_EXPECT(buxn_asm_str(basm, "@scope BRKk @end @BRKk"));
}

BTEST(basm, number) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "2"));
	BTEST_EXPECT(!buxn_asm_str(basm, "123"));
	BTEST_EXPECT(!buxn_asm_str(basm, "12345"));
	BTEST_EXPECT(!buxn_asm_str(basm, "#2"));
	BTEST_EXPECT(!buxn_asm_str(basm, "#123"));
	BTEST_EXPECT(!buxn_asm_str(basm, "#12345"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope #1g"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope #123g"));
}

BTEST(basm, macro) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %label { ADD } %label { SUB }"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %label #1234"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %test { BRK @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %macro {BRK } #1234"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %macro { BRK} #1234"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %add2 { ADD } #1234"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %-test { ADD } #1234"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope %JCN2 { ADD } #1234"));
}

BTEST(basm, references) {
	buxn_asm_ctx_t* basm = &fixture.basm;
	basm->suppress_report = true;

	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT2 =label @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ;label @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope .label @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ,label @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope LIT _label @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@scope ,next $81 @next @end"));
	BTEST_EXPECT(!buxn_asm_str(basm, "@back $7e @scope ,back @end"));
}
