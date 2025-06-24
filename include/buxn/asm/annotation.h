#ifndef BUXN_ANNOTATION_H
#define BUXN_ANNOTATION_H

#include <stddef.h>
#include "asm.h"

typedef enum {
	BUXN_ANNOTATION_IMMEDIATE,
	BUXN_ANNOTATION_PREFIX,
	BUXN_ANNOTATION_POSTFIX,
} buxn_anno_type_t;

typedef enum {
	BUXN_ANNO_COMMENT_MIGHT_BE_TYPE,
	BUXN_ANNO_COMMENT_IS_TYPE,
	BUXN_ANNO_COMMENT_IS_CUSTOM_ANNOTATION,
	BUXN_ANNO_COMMENT_IS_TEXT,
} buxn_anno_comment_kind_t;

typedef struct {
	const char* name;
	buxn_anno_type_t type;

	// For internal use
	buxn_asm_source_region_t region;
} buxn_anno_t;

typedef void (*buxn_anno_handler_t)(
	void* ctx,
	uint16_t addr,
	const buxn_asm_sym_t* sym,
	const buxn_anno_t* annotation,
	const buxn_asm_source_region_t* region
);

typedef struct {
	buxn_anno_t* annotations;
	size_t num_annotations;
	void* ctx;
	buxn_anno_handler_t handler;

	// For internal use
	buxn_asm_sym_t current_def;
	buxn_asm_sym_t last_rom_sym;
	uint16_t current_def_addr;
	uint16_t last_rom_addr;
	buxn_anno_t* current_annotation;
	buxn_asm_sym_t comment_start;
	buxn_asm_sym_t comment_first_token;
	buxn_asm_sym_t comment_last_token;
	buxn_anno_comment_kind_t comment_kind;
} buxn_anno_spec_t;

void
buxn_anno_handle_symbol(buxn_anno_spec_t* spec, uint16_t addr, const buxn_asm_sym_t* sym);

#endif
