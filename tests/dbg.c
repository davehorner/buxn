#include <btest.h>
#include <barena.h>
#include <string.h>
#include <threads.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include <buxn/vm/vm.h>
#include <buxn/devices/mouse.h>
#include <buxn/dbg/core.h>
#include <buxn/dbg/wire.h>
#include <buxn/dbg/protocol.h>
#include <buxn/dbg/transports/fd.h>
#include "resources.h"

#define BTEST_ASSERT_REL(FMT, VALUE, REL, EXPECTATION) \
	BTEST_ASSERT_EX(VALUE REL EXPECTATION, #VALUE " = " FMT, VALUE)

#define BTEST_ASSERT_EQ(FMT, VALUE, EXPECTATION) \
	BTEST_ASSERT_REL(FMT, VALUE, ==, EXPECTATION)

#define BYTE_HEX_FMT "0x%02x"
#define SHORT_HEX_FMT "0x%04x"

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

	uint8_t dbg_value;
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
	fixture.devices.mouse = (buxn_mouse_t){ 0 };
	fixture.devices.system_dbg = NULL;

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
load_file(buxn_vm_t* vm, const char* filename) {
	barena_snapshot_t snapshot = barena_snapshot(&fixture.arena);

	buxn_asm_ctx_t basm = {
		.arena = &fixture.arena,
		.vfs = (buxn_vfs_entry_t[]) {
			{ .name = "door.tal", .content = XINCBIN_GET(door_tal) },
			{ .name = "object.tal", .content = XINCBIN_GET(object_tal) },
			{ .name = "vector.tal", .content = XINCBIN_GET(vector_tal) },
			{ .name = "prog_brkp.tal", .content = XINCBIN_GET(prog_brkp_tal) },
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

static int
vm_thread_entry(void* userdata) {
	buxn_vm_execute(userdata, BUXN_RESET_VECTOR);
	return 0;
}

static void
run_vm_async(buxn_vm_t* vm){
	thrd_create(&fixture.vm_thread, vm_thread_entry, vm);
}

static int
mouse_vector_entry(void* userdata) {
	buxn_mouse_update(userdata);
	return 0;
}

static thrd_t
run_mouse_vector_async(buxn_vm_t* vm){
	thrd_t id;
	thrd_create(&id, mouse_vector_entry, vm);
	return id;
}

static bserial_status_t
next_dbg_msg(buxn_dbg_msg_t* msg) {
	return buxn_dbg_protocol_msg(fixture.dbg_conn.wire.in, fixture.dbg_conn.wire.buffer, msg);
}

#define dbg_command(...) \
	do { \
		buxn_dbg_msg_t msg = { \
			.type = BUXN_DBG_MSG_COMMAND_REQ, \
			.cmd = __VA_ARGS__, \
		}; \
		BTEST_ASSERT(buxn_dbg_protocol_msg(fixture.dbg_conn.wire.out, fixture.dbg_conn.wire.buffer, &msg) == BSERIAL_OK); \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_COMMAND_REP); \
	} while (0)

#define ASSERT_BEGIN_EXEC(ADDR) \
	do { \
		buxn_dbg_msg_t msg; \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_BEGIN_EXEC); \
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, msg.addr, ADDR); \
	} while (0)

#define ASSERT_BEGIN_BREAK(BRKP) \
	do { \
		ASSERT_BEGIN_BREAK_NO_PAUSE(BRKP); \
		ASSERT_PAUSED(); \
	} while (0)

#define ASSERT_BEGIN_BREAK_NO_PAUSE(BRKP) \
	do { \
		buxn_dbg_msg_t msg; \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_BEGIN_BREAK); \
		BTEST_ASSERT_EQ("%d", msg.brkp_id, BRKP); \
	} while (0)

#define ASSERT_PAUSED() \
	do { \
		buxn_dbg_msg_t msg; \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_PAUSED); \
	} while (0)

#define ASSERT_END_BREAK() \
	do { \
		buxn_dbg_msg_t msg; \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_END_BREAK); \
	} while (0)

#define ASSERT_END_EXEC() \
	do { \
		buxn_dbg_msg_t msg; \
		BTEST_ASSERT(next_dbg_msg(&msg) == BSERIAL_OK); \
		BTEST_ASSERT_EQ("%d", msg.type, BUXN_DBG_MSG_END_EXEC); \
	} while (0)

BTEST(dbg, pause) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);

	uint16_t pc;
	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_INFO,
		.info = {
			.type = BUXN_DBG_INFO_PC,
			.pc = &pc,
		},
	});
	BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR);

	dbg_command((buxn_dbg_cmd_t){
		.type = BUXN_DBG_CMD_RESUME,
	});

	ASSERT_END_BREAK();
	ASSERT_END_EXEC();
}

BTEST(dbg, mem_exec_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
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
		BTEST_ASSERT_EQ("%d", nbrkps, 1);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(0);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, mem_exec_brkp_no_pause) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
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
		BTEST_ASSERT_EQ("%d", nbrkps, 1);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK_NO_PAUSE(0);
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, mem_store_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
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
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(1);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, 0x0105);  // STR

		uint8_t byte;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_MEM_READ,
			.mem_read = {
				.addr = 0x0101,  // &door
				.size = 1,
				.values = &byte,
			},
		});
		// The instruction is not executed yet
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, byte, 0x00);

		// Let it execute
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OVER,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint8_t byte;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_MEM_READ,
			.mem_read = {
				.addr = 0x0101,  // &door
				.size = 1,
				.values = &byte,
			},
		});
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, byte, 0x01);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, mem_load_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "object.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x0104,  // &Object/x
					.mask = BUXN_DBG_BRKP_LOAD | BUXN_DBG_BRKP_PAUSE | BUXN_DBG_BRKP_MEM,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(0);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, 0x0108);  // LDA

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OVER,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		buxn_dbg_stack_info_t stack;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_WST,
				.stack = &stack,
			},
		});
		BTEST_ASSERT_EQ("%d", stack.pointer, 1);
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, stack.data[0], 0x42);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, dev_exec_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "vector.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x90,  // .Mouse/vector
					.mask = BUXN_DBG_BRKP_EXEC | BUXN_DBG_BRKP_DEV | BUXN_DBG_BRKP_PAUSE,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();
	ASSERT_END_EXEC();

	thrd_t mouse_thread = run_mouse_vector_async(vm);
	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR + 7);  // @on-mouse
	ASSERT_BEGIN_BREAK(0);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 7);  // @on-mouse

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();
	ASSERT_END_EXEC();

	int res;
	thrd_join(mouse_thread, &res);
}

BTEST(dbg, dev_load_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "vector.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x92,  // Mouse/x
					.mask = BUXN_DBG_BRKP_LOAD | BUXN_DBG_BRKP_DEV | BUXN_DBG_BRKP_PAUSE,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();
	ASSERT_END_EXEC();

	fixture.devices.mouse.x = 42;
	fixture.devices.mouse.y = 69;

	thrd_t mouse_thread = run_mouse_vector_async(vm);
	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR + 7);  // @on-mouse

	ASSERT_BEGIN_BREAK(0);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});

		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 9);  // DEI
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OVER,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		buxn_dbg_stack_info_t stack;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_WST,
				.stack = &stack,
			},
		});
		BTEST_ASSERT_EQ("%d", stack.pointer, 2);
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, stack.data[0], 0x00);
		BTEST_ASSERT_EQ("%d", stack.data[1], 42);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();

	int res;
	thrd_join(mouse_thread, &res);
}

BTEST(dbg, dev_store_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "vector.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x91,  // .Mouse/vector
					.mask = BUXN_DBG_BRKP_STORE | BUXN_DBG_BRKP_DEV | BUXN_DBG_BRKP_PAUSE,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(0);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 5);  // DEO2

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

static void
system_dbg_port(buxn_vm_t* vm, uint8_t value) {
	(void)vm;
	(void)value;
	buxn_dbg_request_pause(fixture.dbg);
}

BTEST(dbg, prog_brkp) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "prog_brkp.tal"));
	fixture.devices.system_dbg = system_dbg_port;
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	// Triggered by the programmed breakpoint
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		// Unlike debugger breakpoint, programmed breakpoint triggers **after**
		// the write to System/dbg
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 5);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, mem_write) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint8_t byte = 0x01;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_MEM_WRITE,
			.mem_write = {
				.addr = 0x0101,  // &door
				.size = 1,
				.values = &byte,
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();

	uint8_t byte = vm->memory[0x101];
	BTEST_ASSERT_EQ(BYTE_HEX_FMT, byte, 0x02);
}

BTEST(dbg, mem_batch_read) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	uint8_t rom[7];
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_MEM_READ,
			.mem_write = {
				.addr = BUXN_RESET_VECTOR,
				.size = sizeof(rom),
				.values = rom,
			},
		});

		BTEST_ASSERT((memcmp(rom, vm->memory + BUXN_RESET_VECTOR, sizeof(rom)) == 0));

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();

	rom[1] = 0x01;  // &door has been overwritten
	BTEST_ASSERT((memcmp(rom, vm->memory + BUXN_RESET_VECTOR, sizeof(rom)) == 0));
}

BTEST(dbg, dev_read) {
	buxn_vm_t* vm = fixture.vm;
	run_vm_async(vm);

	fixture.devices.mouse.x = 42;
	fixture.devices.mouse.y = 69;

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint8_t values[2];
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_DEV_READ,
			.dev_read = {
				.addr = 0x92,  // Mouse/x
				.size = 2,
				.values = values,
			},
		});
		uint16_t mouse_x = (values[0] << 8) | values[1];
		BTEST_ASSERT_EQ("%d", mouse_x, 42);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();
	ASSERT_END_EXEC();
}

static void
system_dbg_dev_write(buxn_vm_t* vm, uint8_t value) {
	fixture.dbg_value = value;
	vm->device[0x0e] = 0x01;
}

BTEST(dbg, dev_write) {
	buxn_vm_t* vm = fixture.vm;
	run_vm_async(vm);
	fixture.devices.system_dbg = system_dbg_dev_write;

	uint16_t value = buxn_vm_dev_load2(vm, 0x00);
	BTEST_ASSERT_EQ(SHORT_HEX_FMT, value, 0x0000);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);
	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint8_t values[2] = { 0x69, 0x42 };
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_DEV_WRITE,
			.dev_read = {
				.addr = 0x00,
				.size = 2,
				.values = values,
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_DEV_WRITE,
			.dev_read = {
				.addr = 0x0e,
				.size = 1,
				.values = &(uint8_t){ 0x03 },
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();
	ASSERT_END_EXEC();

	value = buxn_vm_dev_load2(vm, 0x00);
	BTEST_ASSERT_EQ(SHORT_HEX_FMT, value, 0x6942);

	BTEST_ASSERT_EQ(SHORT_HEX_FMT, fixture.dbg_value, 0x03);
	BTEST_ASSERT_EQ(SHORT_HEX_FMT, vm->device[0x0e], 0x01);
}

BTEST(dbg, step_over) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "object.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OVER,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 3);  // BRK

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_IN,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, step_in_and_out) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "object.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_IN,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 5);  // ;/x

		// The return stack should contain the address of BRK
		buxn_dbg_stack_info_t stack;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_RST,
				.stack = &stack,
			},
		});
		BTEST_ASSERT_EQ("%d", stack.pointer, 2);
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, stack.data[0], 0x01);  // BRK
		BTEST_ASSERT_EQ(BYTE_HEX_FMT, stack.data[1], 0x03);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OUT,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		uint16_t pc;
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_PC,
				.pc = &pc,
			},
		});
		BTEST_ASSERT_EQ(SHORT_HEX_FMT, pc, BUXN_RESET_VECTOR + 3);  // BRK

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_IN,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, step_out_on_top) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "object.tal"));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_STEP_OUT,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, brkp_compaction) {
	buxn_vm_t* vm = fixture.vm;
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x0100,
					.mask = BUXN_DBG_BRKP_EXEC,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 1,
				.brkp = {
					.addr = 0x0101,
					.mask = BUXN_DBG_BRKP_EXEC,
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
		BTEST_ASSERT_EQ("%d", nbrkps, 2);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = { .id = 0 },
		});
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_NBRKPS,
				.nbrkps = &nbrkps,
			},
		});
		BTEST_ASSERT_EQ("%d", nbrkps, 2);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = { .id = 1 },
		});
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_INFO,
			.info = {
				.type = BUXN_DBG_INFO_NBRKPS,
				.nbrkps = &nbrkps,
			},
		});
		BTEST_ASSERT_EQ("%d", nbrkps, 0);

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	ASSERT_END_EXEC();
}

BTEST(dbg, should_hook) {
	buxn_vm_t* vm = fixture.vm;
	BTEST_ASSERT(load_file(vm, "door.tal"));

	BTEST_ASSERT(buxn_dbg_should_hook(fixture.dbg));
	run_vm_async(vm);

	ASSERT_BEGIN_EXEC(BUXN_RESET_VECTOR);

	ASSERT_BEGIN_BREAK(BUXN_DBG_BRKP_NONE);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = {
				.id = 0,
				.brkp = {
					.addr = 0x0102,  // INCk
					.mask = BUXN_DBG_BRKP_EXEC | BUXN_DBG_BRKP_PAUSE,
				},
			},
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	BTEST_ASSERT(buxn_dbg_should_hook(fixture.dbg));

	ASSERT_BEGIN_BREAK(0);
	{
		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_BRKP_SET,
			.brkp_set = { .id = 0 },
		});

		dbg_command((buxn_dbg_cmd_t){
			.type = BUXN_DBG_CMD_RESUME,
		});
	}
	ASSERT_END_BREAK();

	BTEST_ASSERT(!buxn_dbg_should_hook(fixture.dbg));

	ASSERT_END_EXEC();
}
