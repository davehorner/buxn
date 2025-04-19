#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <barena.h>
#include <bhash.h>
#include <errno.h>
#include <blog.h>
#include "asm/asm.h"

typedef struct {
	int len;
	const char* chars;
} str_t;

typedef BHASH_TABLE(uint16_t, str_t) str_table_t;

struct buxn_asm_ctx_s {
	uint16_t rom_size;
	uint16_t num_macros;
	uint16_t num_labels;
	char rom[UINT16_MAX];
	FILE* sym_file;
	barena_t arena;
	str_table_t str_table;
};

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(&ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	uint16_t offset = address - 256;
	ctx->rom[offset] = value;
	ctx->rom_size = offset + 1 > ctx->rom_size ? offset + 1 : ctx->rom_size;
}

void
buxn_asm_put_string(buxn_asm_ctx_t* ctx, uint16_t id, const char* str, int len) {
	bhash_put(&ctx->str_table, id, ((str_t){ .chars = str, .len = len }));
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	if (sym->type == BUXN_ASM_SYM_MACRO) {
		++ctx->num_macros;
	} else if (sym->type == BUXN_ASM_SYM_LABEL) {
		++ctx->num_labels;

		if (ctx->sym_file) {
			bhash_index_t index = bhash_find(&ctx->str_table, sym->id);
			assert(bhash_is_valid(index) && "Assembler reports invalid string");
			str_t str = ctx->str_table.values[index];

			uint8_t addr_hi = addr >> 8;
			uint8_t addr_lo = addr & 0xff;
			fwrite(&addr_hi, sizeof(addr_hi), 1, ctx->sym_file);
			fwrite(&addr_lo, sizeof(addr_hi), 1, ctx->sym_file);
			// All interned string are null-terminated so this is safe
			fwrite(str.chars, str.len + 1, 1, ctx->sym_file);

			if (ferror(ctx->sym_file)) {
				BLOG_ERROR("Error while writing symbol file: %s", strerror(errno));
			}
		}
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	(void)ctx;
	return (void*)fopen(filename, "rb");
}

void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	fclose((void*)file);
}

int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	int result = fgetc((void*)file);
	if (result == EOF) {
		return BUXN_ASM_IO_EOF;
	} else if (result < 0) {
		return BUXN_ASM_IO_ERROR;
	} else {
		return result;
	}
}

void
buxn_asm_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report) {
	// TODO: diagnostic line
	(void)ctx;
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

	if (report->related_region) {
		blog_write(
			level,
			report->related_region->filename, report->related_region->range.start.line,
			"<-- See also"
		);
	}
}

static bool
write_rom(buxn_asm_ctx_t* ctx, const char* rom_path) {
	FILE* rom_file = NULL;
	bool success = true;
	rom_file = fopen(rom_path, "wb");
	if (rom_file == NULL) {
		BLOG_ERROR("Error while opening rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

	if (fwrite(ctx->rom, ctx->rom_size, 1, rom_file) < 0) {
		BLOG_ERROR("Error while writing rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

	if (fflush(rom_file) != 0) {
		BLOG_ERROR("Error while writing rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

end:
	if (rom_file != NULL) { fclose(rom_file); }
	return success;
}

// https://nullprogram.com/blog/2018/07/31/
static uint64_t
prospector32(const void* data, size_t size) {
	(void)size;
	uint32_t x = *(const uint16_t*)data;
	x ^= x >> 15;
	x *= 0x2c1b3c6dU;
	x ^= x >> 12;
	x *= 0x297a2d39U;
	x ^= x >> 15;
	return x;
}

int
main(int argc, const char* argv[]) {
	blog_level_t log_level;
#ifdef _DEBUG
	log_level = BLOG_LEVEL_DEBUG;
#else
	log_level = BLOG_LEVEL_INFO;
#endif
	blog_init(&(blog_options_t){
		.current_filename = __FILE__,
		.current_depth_in_project = 1,
	});
	blog_add_file_logger(log_level, &(blog_file_logger_options_t){
		.file = stderr,
		.with_colors = true,
	});

	if (argc < 3) {
		fprintf(stderr, "Usage: buxn-asm <in.tal> <out.rom>\n");
		return 1;
	}

	barena_pool_t arena_pool;
	barena_pool_init(&arena_pool, 1);

	buxn_asm_ctx_t ctx = { 0 };
	barena_init(&ctx.arena, &arena_pool);

	bhash_config_t config = bhash_config_default();
	config.removable = false;
	config.hash = prospector32;
	bhash_init(&ctx.str_table, config);

	// temp buf for extra filenames
	const char* rom_filename = argv[2];
	size_t rom_filename_len = strlen(rom_filename);
	size_t namebuf_len = rom_filename_len + 5;
	char* namebuf = barena_memalign(&ctx.arena, rom_filename_len + 5, _Alignof(char));

	// .sym file
	{
		snprintf(namebuf, namebuf_len, "%s.sym", rom_filename);
		ctx.sym_file = fopen(namebuf, "wb");
		if (ctx.sym_file == NULL) {
			BLOG_ERROR("Error while opening symbol file: %s", strerror(errno));
		}
	}

	bool success = buxn_asm(&ctx, argv[1]);

	bhash_cleanup(&ctx.str_table);
	barena_reset(&ctx.arena);
	barena_pool_cleanup(&arena_pool);

	success &= write_rom(&ctx, argv[2]);

	if (success) {
		BLOG_INFO(
			"Assembled %s in %d byte(s), %d label(s), %d macro(s)",
			argv[2],
			ctx.rom_size,
			ctx.num_labels,
			ctx.num_macros
		);
	}

	if (ctx.sym_file != NULL) {
		if (fflush(ctx.sym_file) != 0) {
			BLOG_ERROR("Error while opening symbol file: %s", strerror(errno));
		}

		fclose(ctx.sym_file);
	}

	return success ? 0 : 1;
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <blog.h>
#include <bhash.h>
