#ifndef BUXN_CHESS_H
#define BUXN_CHESS_H

// Because chess is better than checker

#include "asm.h"

#define BUXN_CHESS_NO_TRACE ((buxn_chess_id_t)0)
#define BUXN_CHESS_MAX_ARGS 8

typedef struct buxn_chess_s buxn_chess_t;
typedef int buxn_chess_id_t;
typedef struct buxn_chess_value_s buxn_chess_value_t;
typedef struct buxn_chess_signature_s buxn_chess_signature_t;

typedef enum {
	BUXN_CHESS_REPORT_TRACE,
	BUXN_CHESS_REPORT_WARNING,
	BUXN_CHESS_REPORT_ERROR,
} buxn_chess_report_type_t;

typedef enum {
	BUXN_CHESS_VECTOR,
	BUXN_CHESS_SUBROUTINE,
} buxn_chess_routine_type_t;

typedef struct {
	const char* chars;
	uint8_t len;
} buxn_chess_str_t;

struct buxn_chess_value_s {
	buxn_chess_str_t name;
	buxn_chess_str_t type;
	buxn_chess_signature_t* signature;
	buxn_chess_value_t* whole_value;
	buxn_asm_source_region_t region;
	uint16_t value;
	uint16_t semantics;
};

typedef struct {
	buxn_chess_value_t content[BUXN_CHESS_MAX_ARGS];
	uint8_t len;
} buxn_chess_sig_stack_t;

struct buxn_chess_signature_s {
	buxn_chess_sig_stack_t wst_in;
	buxn_chess_sig_stack_t rst_in;
	buxn_chess_sig_stack_t wst_out;
	buxn_chess_sig_stack_t rst_out;
	buxn_asm_source_region_t region;
	buxn_chess_routine_type_t type;
};

typedef struct {
	uint8_t len;
	uint8_t size;
	buxn_chess_value_t content[256];
} buxn_chess_stack_t;

typedef struct {
	buxn_chess_stack_t wst;
	buxn_chess_stack_t rst;
	buxn_asm_source_region_t src_region;
	uint16_t pc;
} buxn_chess_vm_state_t;

buxn_chess_t*
buxn_chess_begin(buxn_asm_ctx_t* ctx);

bool
buxn_chess_end(buxn_chess_t* chess);

void
buxn_chess_handle_symbol(
	buxn_chess_t* chess,
	uint16_t addr,
	const buxn_asm_sym_t* sym
);

// Must be provided by the host program

extern uint8_t
buxn_chess_get_rom(buxn_asm_ctx_t* ctx, uint16_t address);

extern void*
buxn_chess_begin_mem_region(buxn_asm_ctx_t* ctx);

extern void
buxn_chess_end_mem_region(buxn_asm_ctx_t* ctx, void* region);

extern void*
buxn_chess_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment);

extern void
buxn_chess_begin_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_id_t parent_id
);

extern void
buxn_chess_end_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	bool success
);

extern void
buxn_chess_report(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_report_type_t type,
	const buxn_asm_report_t* report
);

extern void
buxn_chess_deo(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const buxn_chess_vm_state_t* state,
	uint8_t value,
	uint8_t port
);

#endif
