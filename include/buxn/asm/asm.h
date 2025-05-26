#ifndef BUXN_ASM_H
#define BUXN_ASM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BUXN_ASM_IO_EOF   -1
#define BUXN_ASM_IO_ERROR -2

typedef struct buxn_asm_file_s buxn_asm_file_t;
typedef struct buxn_asm_ctx_s buxn_asm_ctx_t;

typedef struct {
	int line;
	int col;
	int byte;
} buxn_asm_file_pos_t;

typedef struct {
	buxn_asm_file_pos_t start;
	buxn_asm_file_pos_t end;
} buxn_asm_file_range_t;

typedef enum {
	BUXN_ASM_SYM_MACRO,
	BUXN_ASM_SYM_LABEL,
	BUXN_ASM_SYM_LABEL_REF,
	BUXN_ASM_SYM_OPCODE,
	BUXN_ASM_SYM_NUMBER,
	BUXN_ASM_SYM_TEXT,
} buxn_asm_sym_type_t;

typedef struct {
	const char* filename;
	buxn_asm_file_range_t range;
} buxn_asm_source_region_t;

typedef struct {
	buxn_asm_sym_type_t type;
	bool name_is_generated;
	uint16_t id;
	const char* name;
	buxn_asm_source_region_t region;
} buxn_asm_sym_t;

typedef enum {
	BUXN_ASM_REPORT_ERROR,
	BUXN_ASM_REPORT_WARNING,
} buxn_asm_report_type_t;

typedef struct {
	const char* message;
	const char* token;
	const buxn_asm_source_region_t* region;

	const char* related_message;
	const buxn_asm_source_region_t* related_region;
} buxn_asm_report_t;

bool
buxn_asm(buxn_asm_ctx_t* ctx, const char* filename);

// Must be provided by the host program

extern void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment);

extern void
buxn_asm_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report);

extern void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t addr, uint8_t value);

extern void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym);

extern buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename);

extern void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file);

extern int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file);

#endif
