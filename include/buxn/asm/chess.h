#ifndef BUXN_CHESS_H
#define BUXN_CHESS_H

// Because chess is better than checker

#include "asm.h"

typedef struct buxn_chess_s buxn_chess_t;
typedef int buxn_chess_id_t;

#define BUXN_CHESS_NO_TRACE ((buxn_chess_id_t)0)

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
	buxn_asm_report_type_t type,
	const buxn_asm_report_t* report
);

extern void
buxn_chess_report_info(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const buxn_asm_report_t* report
);

extern void
buxn_chess_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const char* filename,
	int line,
	const char* fmt,
	...
);

#endif
