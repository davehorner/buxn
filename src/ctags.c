#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <barena.h>
#include <blog.h>
#include "asm/asm.h"

typedef struct {
	int len;
	const char* chars;
} str_t;

struct buxn_asm_ctx_s {
	FILE* tag_file;
	barena_t arena;
};

static void
export_tag(buxn_asm_ctx_t* ctx, const buxn_asm_sym_t* sym, char kind) {
	fprintf(
		ctx->tag_file,
		"%s\t%s\tgo %d|;\"\t%c\n",
		sym->name, sym->region.filename, sym->region.range.start.byte + 1, kind
	);
}

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(&ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	(void)ctx;
	(void)address;
	(void)value;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	(void)addr;

	if (sym->type == BUXN_ASM_SYM_MACRO) {
		export_tag(ctx, sym, 'm');
	} else if (sym->type == BUXN_ASM_SYM_LABEL && !sym->name_is_generated) {
		export_tag(ctx, sym, 'l');
	}
}

void
buxn_asm_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report) {
	(void)ctx;
	(void)type;

	if (report->token == NULL) {
		blog_write(
			BLOG_LEVEL_WARN,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	} else {
		blog_write(
			BLOG_LEVEL_WARN,
			report->region->filename, report->region->range.start.line,
			"%s (`%s`)", report->message, report->token
		);
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

	int exit_code = 1;

	if (argc < 2) {
		fprintf(stderr, "Usage: buxn-ctag <entry.tal>\n");
		return 1;
	}

	// TODO: make this configurable

	barena_pool_t arena_pool;
	barena_pool_init(&arena_pool, 1);

	buxn_asm_ctx_t ctx = { 0 };
	barena_init(&ctx.arena, &arena_pool);

	ctx.tag_file = fopen("tags", "wb");
	if (ctx.tag_file == NULL) {
		BLOG_ERROR("Error while opening tag file: %s", strerror(errno));
		goto end;
	}

	bool success = buxn_asm(&ctx, argv[1]);
	if (!success) {
		BLOG_WARN("Error(s) encountered, tags file may be incomplete");
	}

	exit_code = 0;
end:
	barena_reset(&ctx.arena);
	barena_pool_cleanup(&arena_pool);

	if (ctx.tag_file != NULL) {
		fclose(ctx.tag_file);
	}

	return exit_code;
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <blog.h>
