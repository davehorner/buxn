#include "common.h"
#include <buxn/vm/vm.h>
#include <buxn/devices/system.h>
#include <string.h>
#include <stdlib.h>
#include <blog.h>

struct buxn_asm_file_s {
	const char* content;
	size_t size;
	size_t pos;
};

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	uint16_t offset = address - 256;
	ctx->rom[offset] = value;
	ctx->rom_size = offset + 1 > ctx->rom_size ? offset + 1 : ctx->rom_size;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	if (ctx->chess != NULL) {
		buxn_chess_handle_symbol(ctx->chess, addr, sym);
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	for (buxn_vfs_entry_t* entry = ctx->vfs; entry->name != NULL; ++entry) {
		if (strcmp(entry->name, filename) == 0) {
			buxn_asm_file_t* file = malloc(sizeof(buxn_asm_file_t));
			file->content = (const char*)entry->content.data;
			file->size = entry->content.size;
			file->pos = 0;
			return file;
		}
	}

	return NULL;
}

void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	free(file);
}

int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	if (file->pos >= file->size) {
		return BUXN_ASM_IO_EOF;
	} else {
		return (int)file->content[file->pos++];
	}
}

void
buxn_asm_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report) {
	switch (type) {
		case BUXN_ASM_REPORT_ERROR: ++ctx->num_errors; break;
		case BUXN_ASM_REPORT_WARNING: ++ctx->num_warnings; break;
	}

	if (ctx->suppress_report) { return; }

	blog_level_t level = BLOG_LEVEL_INFO;
	switch (type) {
		case BUXN_ASM_REPORT_ERROR: level = BLOG_LEVEL_ERROR; break;
		case BUXN_ASM_REPORT_WARNING: level = BLOG_LEVEL_WARN; break;
	}
	if (report->token == NULL) {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	} else {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s (`%s`)", report->message, report->token
		);
	}
}

void*
buxn_chess_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return buxn_asm_alloc(ctx, size, alignment);
}

uint8_t
buxn_chess_get_rom(buxn_asm_ctx_t* ctx, uint16_t address) {
	return ctx->rom[address - 256];
}

void*
buxn_chess_begin_mem_region(buxn_asm_ctx_t* ctx) {
	return (void*)barena_snapshot(ctx->arena);
}

void
buxn_chess_end_mem_region(buxn_asm_ctx_t* ctx, void* region) {
	barena_restore(ctx->arena, (barena_snapshot_t)region);
}

void
buxn_chess_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report) {
	buxn_asm_report(ctx, type, report);
}

void
buxn_chess_report_info(buxn_asm_ctx_t* ctx, const buxn_asm_report_t* report) {
	(void)ctx;
	(void)report;
}

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	buxn_test_devices_t* devices = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_MOUSE:
			return buxn_mouse_dei(vm, &devices->mouse, address);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	buxn_test_devices_t* devices = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &devices->console, address);
			break;
		case BUXN_DEVICE_MOUSE:
			buxn_mouse_deo(vm, &devices->mouse, address);
			break;
	}
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
	buxn_test_devices_t* devices = vm->config.userdata;
	if (devices->system_dbg) {
		devices->system_dbg(vm, value);
	}
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
