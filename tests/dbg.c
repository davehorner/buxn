#include <btest.h>
#include <barena.h>
#include <string.h>
#include <threads.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "../src/vm/vm.h"
#include "../src/dbg/core.h"
#include "../src/dbg/wire.h"
#include "../src/dbg/protocol.h"
#include "../src/dbg/transports/fd.h"

typedef struct {
	buxn_dbg_wire_t wire;
	buxn_dbg_transport_fd_t transport;
} debug_conn_t;

static struct {
	barena_pool_t pool;
	barena_t arena;

	buxn_vm_t* vm;
	buxn_test_devices_t devices;

	buxn_dbg_t* dbg;
	thrd_t vm_thread;
	debug_conn_t dbg_conn, vm_conn;
} fixture;

static void
exec_hook(buxn_vm_t* vm, uint16_t pc) {
	buxn_dbg_hook(fixture.dbg, vm, pc);
}

static void
init_per_suite(void) {
	barena_pool_init(&fixture.pool, 1);
}

static void
cleanup_per_suite(void) {
	barena_pool_cleanup(&fixture.pool);
}

static void
init_dbg_conn(debug_conn_t* conn, int fd) {
	buxn_dbg_transport_fd_init(&conn->transport, fd);

	bserial_ctx_config_t config = buxn_dbg_protocol_recommended_bserial_config();
	conn->wire = (buxn_dbg_wire_t) { 0 };
	void* dbg_in_mem = barena_malloc(&fixture.arena, bserial_ctx_mem_size(config));
	void* dbg_out_mem = barena_malloc(&fixture.arena, bserial_ctx_mem_size(config));
	buxn_dbg_transport_fd_wire(&conn->transport, &conn->wire, config, dbg_in_mem, dbg_out_mem);
}

static void
cleanup_dbg_conn(debug_conn_t* conn) {
	// shutdown ensures that read/write will fail and the vm will bail out of
	// debug wait regardless of status
	shutdown(conn->transport.fd, SHUT_RDWR);
}

static void
init_per_test(void) {
	barena_init(&fixture.arena, &fixture.pool);

	int sockpairs[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sockpairs);

	init_dbg_conn(&fixture.vm_conn, sockpairs[0]);
	init_dbg_conn(&fixture.dbg_conn, sockpairs[1]);

	fixture.dbg = buxn_dbg_init(
		barena_memalign(&fixture.arena, BUXN_DBG_SIZE, BUXN_DBG_ALIGNMENT),
		&fixture.vm_conn.wire
	);
	buxn_dbg_request_pause(fixture.dbg);

	buxn_console_init(fixture.vm, &fixture.devices.console, 0, NULL);

	fixture.vm = barena_malloc(&fixture.arena, sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE);
	fixture.vm->memory_size = BUXN_MEMORY_BANK_SIZE;
	fixture.vm->userdata = &fixture.devices;
	fixture.vm->exec_hook = exec_hook;
	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);
}

static void
cleanup_per_test(void) {
	cleanup_dbg_conn(&fixture.dbg_conn);
	cleanup_dbg_conn(&fixture.vm_conn);

	int status;
	thrd_join(fixture.vm_thread, &status);

	barena_reset(&fixture.arena);
}

static btest_suite_t dbg = {
	.name = "dbg",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

static inline bool
load_str(buxn_vm_t* vm, const char* str, const char* file, int line) {
	barena_snapshot_t snapshot = barena_snapshot(&fixture.arena);

	int size = snprintf(NULL, 0, "%s:%d", file, line);
	char* filename = barena_malloc(&fixture.arena, size + 1);
	snprintf(filename, size + 1, "%s:%d", file, line);

	buxn_asm_ctx_t basm = {
		.arena = &fixture.arena,
		.vfs = (buxn_vfs_entry_t[]) {
			{
				.name = filename,
				.content = { .data = (const unsigned char*)str, .size = strlen(str) }
			},
			{ 0 },
		},
	};

	bool result = buxn_asm(&basm, filename);
	if (result) {
		memcpy(vm->memory + BUXN_RESET_VECTOR, basm.rom, basm.rom_size);
	}

	barena_restore(&fixture.arena, snapshot);

	return result;
}

#define load_str(vm, str) load_str(vm, str, __FILE__, __LINE__)

static int
vm_thread_entry(void* userdata) {
	buxn_vm_execute(userdata, BUXN_RESET_VECTOR);
	return 0;
}

static void
run_vm_async(buxn_vm_t* vm){
	thrd_create(&fixture.vm_thread, vm_thread_entry, vm);
}

static bserial_status_t
next_dbg_msg(buxn_dbg_msg_t* msg) {
	return buxn_dbg_protocol_msg(fixture.dbg_conn.wire.in, fixture.dbg_conn.wire.buffer, msg);
}

static void
dbg_command(buxn_dbg_cmd_t cmd) {
	buxn_dbg_msg_t msg = {
		.type = BUXN_DBG_MSG_COMMAND_REQ,
		.cmd = cmd,
	};

	BTEST_ASSERT(buxn_dbg_protocol_msg(fixture.dbg_conn.wire.out, fixture.dbg_conn.wire.buffer, &msg) == BSERIAL_OK);
	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT_EX(msg.type == BUXN_DBG_MSG_COMMAND_REP, "msg.type = %d", msg.type);
}

BTEST(dbg, pause) {
	buxn_vm_t* vm = fixture.vm;

	BTEST_ASSERT(load_str(fixture.vm, "[ LIT &door $1 ] INCk ,&door STR"));
	run_vm_async(vm);

	buxn_dbg_msg_t msg;

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_EXEC);
	BTEST_ASSERT_EX(msg.addr == BUXN_RESET_VECTOR, "msg.addr = 0x%04x", msg.addr);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_BREAK);
	BTEST_ASSERT_EX(msg.brkp_id == BUXN_DBG_BRKP_NONE, "msg.brkp_id = %d", msg.brkp_id);

	uint16_t pc;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_INFO,
		.info = {
			.type = BUXN_DBG_INFO_PC,
			.pc = &pc,
		},
	});
	BTEST_ASSERT_EX(pc == BUXN_RESET_VECTOR, "pc = %d", pc);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_EXEC);
}

BTEST(dbg, mem_exec_brkp) {
	buxn_vm_t* vm = fixture.vm;

	BTEST_ASSERT(load_str(fixture.vm, "[ LIT &door $1 ] INCk ,&door STR"));
	run_vm_async(vm);

	buxn_dbg_msg_t msg;

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_EXEC);
	BTEST_ASSERT(msg.addr == BUXN_RESET_VECTOR);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_BREAK);
	BTEST_ASSERT(msg.brkp_id == BUXN_DBG_BRKP_NONE);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_BRKP_SET,
		.brkp_set = {
			.id = 0,
			.brkp = {
				.addr = 0x0102,  // INCk
				.mask = BUXN_DBG_BRKP_EXEC | BUXN_DBG_BRKP_PAUSE | BUXN_DBG_BRKP_MEM,
			},
		},
	});

	uint8_t nbrkps;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_INFO,
		.info = {
			.type = BUXN_DBG_INFO_NBRKPS,
			.nbrkps = &nbrkps,
		},
	});
	BTEST_ASSERT_EX(nbrkps == 1, "nbrkps = %d", nbrkps);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT_EX(msg.type == BUXN_DBG_MSG_BEGIN_BREAK, "msg.type = %d", msg.type);
	BTEST_ASSERT_EX(msg.brkp_id == 0, "msg.brkp_id = %d", msg.brkp_id);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);
	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_EXEC);
}

BTEST(dbg, mem_exec_brkp_no_pause) {
	buxn_vm_t* vm = fixture.vm;

	BTEST_ASSERT(load_str(fixture.vm, "[ LIT &door $1 ] INCk ,&door STR"));
	run_vm_async(vm);

	buxn_dbg_msg_t msg;

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_EXEC);
	BTEST_ASSERT(msg.addr == BUXN_RESET_VECTOR);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_BREAK);
	BTEST_ASSERT(msg.brkp_id == BUXN_DBG_BRKP_NONE);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_BRKP_SET,
		.brkp_set = {
			.id = 0,
			.brkp = {
				.addr = 0x0102,  // INCk
				.mask = BUXN_DBG_BRKP_EXEC | BUXN_DBG_BRKP_MEM,
			},
		},
	});

	uint8_t nbrkps;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_INFO,
		.info = {
			.type = BUXN_DBG_INFO_NBRKPS,
			.nbrkps = &nbrkps,
		},
	});
	BTEST_ASSERT_EX(nbrkps == 1, "nbrkps = %d", nbrkps);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT_EX(msg.type == BUXN_DBG_MSG_BEGIN_BREAK, "msg.type = %d", msg.type);
	BTEST_ASSERT_EX(msg.brkp_id == 0, "msg.brkp_id = %d", msg.brkp_id);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);
	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_EXEC);
}

BTEST(dbg, mem_store_brkp) {
	buxn_vm_t* vm = fixture.vm;

	BTEST_ASSERT(load_str(fixture.vm, "[ LIT &door $1 ] INCk ,&door STR"));
	run_vm_async(vm);

	buxn_dbg_msg_t msg;

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_EXEC);
	BTEST_ASSERT(msg.addr == BUXN_RESET_VECTOR);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_BREAK);
	BTEST_ASSERT(msg.brkp_id == BUXN_DBG_BRKP_NONE);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_BRKP_SET,
		.brkp_set = {
			.id = 1,
			.brkp = {
				.addr = 0x0101,  // &door
				.mask = BUXN_DBG_BRKP_STORE | BUXN_DBG_BRKP_MEM | BUXN_DBG_BRKP_PAUSE,
			},
		},
	});

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT_EX(msg.type == BUXN_DBG_MSG_BEGIN_BREAK, "msg.type = %d", msg.type);
	BTEST_ASSERT_EX(msg.brkp_id == 1, "msg.brkp_id = %d", msg.brkp_id);

	uint16_t pc;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_INFO,
		.info = {
			.type = BUXN_DBG_INFO_PC,
			.pc = &pc,
		},
	});
	BTEST_ASSERT_EX(pc == 0x0105, "pc = %d", pc);  // STR

	uint8_t byte;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_MEM_READ,
		.mem_read = {
			.addr = 0x0101,
			.size = 1,
			.values = &byte,
		},
	});
	// The instruction is not executed yet
	BTEST_ASSERT_EX(byte == 0x00, "byte = %d", byte);

	// Let it execute
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_STEP_OVER,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);
	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_BEGIN_BREAK);
	BTEST_ASSERT_EX(msg.brkp_id == BUXN_DBG_BRKP_NONE, "msg.brkp_id = %d", msg.brkp_id);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_MEM_READ,
		.mem_read = {
			.addr = 0x0101,
			.size = 1,
			.values = &byte,
		},
	});
	BTEST_ASSERT_EX(byte == 0x01, "byte = %d", byte);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_BREAK);
	BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK);
	BTEST_ASSERT(msg.type == BUXN_DBG_MSG_END_EXEC);
}
