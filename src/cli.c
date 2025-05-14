#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <physfs.h>
#include <errno.h>
#include "vm/vm.h"
#include "dbg/core.h"
#include "dbg/wire.h"
#include "dbg/transports/fd.h"
#include "bembd.h"
#include "devices/console.h"
#include "devices/system.h"
#include "devices/datetime.h"
#include "devices/file.h"

typedef struct {
	buxn_dbg_t* dbg;
	buxn_console_t console;
	buxn_file_t file[BUXN_NUM_FILE_DEVICES];
} vm_data_t;

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	vm_data_t* devices = vm->userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			return buxn_file_dei(
				vm,
				devices->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	vm_data_t* devices = vm->userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &devices->console, address);
			break;
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			buxn_file_deo(
				vm,
				devices->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
			break;
	}
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
	if (value == 0) { return; }

	fprintf(stderr, "WST");
	for (uint8_t i = 0; i < vm->wsp; ++i) {
		fprintf(stderr, " %02hhX", vm->ws[i]);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "RST");
	for (uint8_t i = 0; i < vm->rsp; ++i) {
		fprintf(stderr, " %02hhX", vm->rs[i]);
	}
	fprintf(stderr, "\n");
}

void
buxn_system_set_metadata(buxn_vm_t* vm, uint16_t address) {
	(void)vm;
	(void)address;
}

void
buxn_system_theme_changed(buxn_vm_t* vm) {
	(void)vm;
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stdout);
}

void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stderr);
}

static void
vm_exec_hook(buxn_vm_t* vm, uint16_t pc) {
	vm_data_t* userdata = vm->userdata;
	buxn_dbg_hook(userdata->dbg, vm, pc);
}

static int
boot(int argc, const char* argv[], FILE* rom_file, uint32_t rom_size) {
	int exit_code = 0;
	vm_data_t devices = { 0 };

	_Alignas(BUXN_DBG_ALIGNMENT) char dbg_mem[BUXN_DBG_SIZE];
	buxn_dbg_wire_t wire = { 0 };
	buxn_dbg_transport_fd_t transport;
	void* dbg_in_mem = NULL;
	void* dbg_out_mem = NULL;
	int dbg_fd = -1;
	{
		const char* debug_fd_env = getenv("BUXN_DEBUG_FD");
		if (debug_fd_env != NULL) {
			errno = 0;
			long fd = strtol(debug_fd_env, NULL, 10);
			if (errno == 0 && fd >= 0 && fd < INT32_MAX) {
				dbg_fd = (int)fd;
			}
		}

		if (dbg_fd >= 0) {
			buxn_dbg_transport_fd_init(&transport, dbg_fd);

			bserial_ctx_config_t config = buxn_dbg_protocol_recommended_bserial_config();
			dbg_in_mem = malloc(bserial_ctx_mem_size(config));
			dbg_out_mem = malloc(bserial_ctx_mem_size(config));
			buxn_dbg_transport_fd_wire(&transport, &wire, config, dbg_in_mem, dbg_out_mem);

			devices.dbg = buxn_dbg_init(dbg_mem, &wire);
			buxn_dbg_request_pause(devices.dbg);
		}
	}

	buxn_vm_t* vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	vm->userdata = &devices;
	vm->memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS;
	vm->exec_hook = NULL;
	buxn_vm_reset(vm, BUXN_VM_RESET_ALL);

	// Read rom
	{
		uint8_t* read_pos = &vm->memory[BUXN_RESET_VECTOR];
		if (rom_size == 0) {
			while (read_pos < vm->memory + vm->memory_size) {
				size_t num_bytes = fread(read_pos, 1, 1024, rom_file);
				if (num_bytes == 0) { break; }
				read_pos += num_bytes;
			}
		} else {
			fread(read_pos, rom_size, 1, rom_file);
		}
	}
	fclose(rom_file);

	buxn_console_init(vm, &devices.console, argc, argv);

	if (devices.dbg != NULL) {
		vm->exec_hook = vm_exec_hook;
	}

	buxn_vm_execute(vm, BUXN_RESET_VECTOR);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	buxn_console_send_args(vm, &devices.console);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	while (
		buxn_system_exit_code(vm) < 0
		&& buxn_console_should_send_input(vm)
	) {
		int ch = fgetc(stdin);
		if (ch != EOF) {
			buxn_console_send_input(vm, &devices.console, ch);
		} else {
			buxn_console_send_input_end(vm, &devices.console);
			break;
		}
	}

	exit_code = buxn_system_exit_code(vm);
	if (exit_code < 0) { exit_code = 0; }
end:
	free(vm);
	free(dbg_in_mem);
	free(dbg_out_mem);

	PHYSFS_deinit();
	return exit_code;
}

static int
cli_main(int argc, const char* argv[]) {
	PHYSFS_init(argv[0]);
	PHYSFS_mount(".", "", 1);
	PHYSFS_setWriteDir(".");

	if (argc < 2) {
		fprintf(stderr, "Usage: buxn-cli <rom>\n");
		return 1;
	}
	int exit_code = 0;

	FILE* rom_file;
	if ((rom_file = fopen(argv[1], "rb")) == NULL) {
		perror("Error while opening rom file");
		exit_code = 1;
		goto end;
	}

	exit_code = boot(argc - 2, argv + 2, rom_file, 0);

end:
	PHYSFS_deinit();
	return exit_code;
}

static int
embd_main(int argc, const char* argv[], FILE* rom_file, uint32_t rom_size) {
	PHYSFS_init(argv[0]);
	PHYSFS_mount(".", "", 1);
	PHYSFS_setWriteDir(".");

	int exit_code = boot(argc - 1, argv + 1, rom_file, rom_size);

	PHYSFS_deinit();
	return exit_code;
}

int
main(int argc, const char* argv[]) {
	if (argc == 0) { return cli_main(argc, argv); }

	FILE* self = fopen(argv[0], "rb");
	if (self == NULL) { return cli_main(argc, argv); }

	uint32_t rom_size = bembd_find(self);
	if (rom_size == 0) {
		fclose(self);
		return cli_main(argc, argv);
	} else {
		return embd_main(argc, argv, self, rom_size);
	}
}

#define BLIB_IMPLEMENTATION
#include <blog.h>
#include <bserial.h>
