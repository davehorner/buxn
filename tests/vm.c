#include <btest.h>
#include <barena.h>
#include <string.h>
#include "common.h"
#include "../src/vm/vm.h"
#include "../src/devices/system.h"
#include "resources.h"

static struct {
	barena_pool_t pool;
	barena_t arena;
	buxn_vm_t* vm;
	buxn_test_devices_t devices;
} fixture;

static void
init(void) {
	barena_pool_init(&fixture.pool, 1);
	barena_init(&fixture.arena, &fixture.pool);
	fixture.vm = barena_memalign(
		&fixture.arena,
		sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE,
		_Alignof(buxn_vm_t)
	);
	fixture.vm->memory_size = BUXN_MEMORY_BANK_SIZE;
	fixture.vm->userdata = &fixture.devices;
	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);
	buxn_console_init(fixture.vm, &fixture.devices.console, 0, NULL);
}

static void
cleanup(void) {
	barena_reset(&fixture.arena);
	barena_pool_cleanup(&fixture.pool);
}

static btest_suite_t vm = {
	.name = "vm",
	.init = init,
	.cleanup = cleanup,
};

BTEST(vm, opctest) {
	// TODO: check output
	buxn_asm_ctx_t basm = {
		.arena = &fixture.arena,
		.vfs = (buxn_vfs_entry_t[]) {
			{ .name = "opctest.tal", .content = XINCBIN_GET(opctest_tal) },
			{ 0 },
		}
	};

	BTEST_ASSERT(buxn_asm(&basm, "opctest.tal"));

	memcpy(fixture.vm->memory + BUXN_RESET_VECTOR, basm.rom, basm.rom_size);
	buxn_vm_execute(fixture.vm, BUXN_RESET_VECTOR);
	BTEST_ASSERT(buxn_system_exit_code(fixture.vm) <= 0);
}
