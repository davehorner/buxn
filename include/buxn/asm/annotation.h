#ifndef BUXN_ANNOTATION_H
#define BUXN_ANNOTATION_H

#include <stddef.h>
#include "asm.h"

typedef struct buxn_anno_ctx_s buxn_anno_ctx_t;

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

typedef struct {
	buxn_anno_ctx_t* ctx;
	buxn_anno_t* annotations;
	size_t num_annotations;

	// For internal use
	buxn_asm_sym_t current_sym;
	buxn_anno_t* current_annotation;
	buxn_asm_sym_t comment_start;
	buxn_asm_sym_t comment_first_token;
	buxn_asm_sym_t comment_last_token;
	buxn_anno_comment_kind_t comment_kind;
} buxn_anno_spec_t;

void
buxn_anno_handle_symbol(buxn_anno_spec_t* spec, const buxn_asm_sym_t* sym);

// Must be provided by the host program

extern void
buxn_anno_handle_custom(
	buxn_anno_ctx_t* ctx,
	const buxn_anno_t* annotation,
	const buxn_asm_sym_t* sym,
	const buxn_asm_source_region_t* region
);

extern void
buxn_anno_handle_type(
	buxn_anno_ctx_t* ctx,
	const buxn_asm_sym_t* sym,
	const buxn_asm_source_region_t* region
);

#endif
