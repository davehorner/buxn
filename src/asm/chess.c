// vim: set foldmethod=marker foldlevel=0:
#include <buxn/asm/chess.h>
#include <buxn/vm/opcodes.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <bmacro.h>
#include <stdarg.h>
#define BHAMT_HASH_TYPE uint32_t
#include "hamt.h"

#ifndef BUXN_CHESS_DEBUG
#	ifdef NDEBUG
#		define BUXN_CHESS_DEBUG(...)
#	else
#		include <blog.h>
#		define BUXN_CHESS_DEBUG(...) BLOG_TRACE(__VA_ARGS__)
#	endif
#endif

#define BUXN_CHESS_MAX_ARG_LEN 16
#define BUXN_CHESS_MAX_ARGS 8
#define BUXN_CHESS_MAX_SIG_TOKENS (BUXN_CHESS_MAX_ARGS * 4 + 1)

#define BUXN_CHESS_MEM_NONE        (0)
#define BUXN_CHESS_MEM_INSTRUCTION (1 << 0)
#define BUXN_CHESS_MEM_DATA        (1 << 1)

#define BUXN_CHESS_SEM_SIZE_MASK  0x01
#define BUXN_CHESS_SEM_SIZE_BYTE  (0 << 0)
#define BUXN_CHESS_SEM_SIZE_SHORT (1 << 0)
#define BUXN_CHESS_SEM_CONST      (1 << 1)
#define BUXN_CHESS_SEM_ADDRESS    (1 << 2)
#define BUXN_CHESS_SEM_RETURN     (1 << 3)
#define BUXN_CHESS_SEM_ROUTINE    (1 << 4)
#define BUXN_CHESS_SEM_NOMINAL    (1 << 5)

#define BUXN_CHESS_OP_K 0x80
#define BUXN_CHESS_OP_R 0x40
#define BUXN_CHESS_OP_2 0x20

#define BUXN_CHESS_ADDR_EQ(LHS, RHS) (LHS == RHS)

#define DEFINE_OPCODE_NAME(NAME, VALUE) \
	[VALUE] = BSTRINGIFY(NAME),

static const char* buxn_chess_opcode_names[256] = {
	BUXN_OPCODE_DISPATCH(DEFINE_OPCODE_NAME)
};

typedef void (*buxn_chess_anno_handler_t)(buxn_chess_t* chess, const buxn_asm_sym_t* sym);

typedef struct {
	const char* chars;
	uint8_t len;
} buxn_chess_str_t;

typedef struct buxn_chess_signature_s buxn_chess_signature_t;

typedef struct {
	buxn_chess_str_t name;
	buxn_chess_str_t type;
	buxn_chess_signature_t* signature;
	uint16_t value;
	uint8_t semantics;
} buxn_chess_value_t;

typedef enum {
	BUXN_CHESS_VECTOR,
	BUXN_CHESS_SUBROUTINE,
} buxn_chess_routine_type_t;

typedef enum {
	BUXN_CHESS_PARSE_WST_IN,
	BUXN_CHESS_PARSE_RST_IN,
	BUXN_CHESS_PARSE_WST_OUT,
	BUXN_CHESS_PARSE_RST_OUT,
} buxn_chess_sig_parse_state_t;

typedef struct {
	buxn_chess_value_t content[BUXN_CHESS_MAX_ARGS];
	uint8_t len;
} buxn_chess_sig_stack_t;

struct buxn_chess_signature_s {
	buxn_chess_sig_stack_t wst_in;
	buxn_chess_sig_stack_t rst_in;
	buxn_chess_sig_stack_t wst_out;
	buxn_chess_sig_stack_t rst_out;
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
} buxn_chess_state_t;

typedef struct buxn_chess_addr_info_s buxn_chess_addr_info_t;

struct buxn_chess_addr_info_s {
	uint16_t key;
	buxn_chess_addr_info_t* children[BHAMT_NUM_CHILDREN];

	buxn_chess_value_t value;
	bool queued;
};

typedef struct {
	buxn_chess_addr_info_t* root;
} buxn_chess_addr_map_t;

typedef struct buxn_chess_jump_arc_s buxn_chess_jump_arc_t;

struct buxn_chess_jump_arc_s {
	uint32_t key;
	buxn_chess_jump_arc_t* children[BHAMT_NUM_CHILDREN];
};

typedef struct {
	buxn_chess_jump_arc_t* root;
} buxn_chess_jump_map_t;

typedef struct buxn_chess_entry_s buxn_chess_entry_t;

struct buxn_chess_entry_s {
	buxn_chess_entry_t* next;
	const buxn_chess_addr_info_t* info;
	buxn_chess_state_t state;
	uint16_t address;
};

typedef struct {
	buxn_chess_t* chess;
	buxn_chess_entry_t* entry;

	buxn_chess_stack_t* wsp;
	buxn_chess_stack_t* rsp;
	buxn_chess_stack_t init_wst;
	buxn_chess_stack_t init_rst;

	const buxn_asm_sym_t* start_sym;
	const buxn_asm_sym_t* current_sym;
	uint16_t pc;
	uint8_t current_opcode;
	bool terminated;
	buxn_asm_source_region_t error_region;
	bool entry_reported;
} buxn_chess_exec_ctx_t;

struct buxn_chess_s {
	buxn_asm_ctx_t* ctx;
	buxn_chess_str_t args;
	buxn_chess_anno_handler_t anno_handler;

	buxn_asm_sym_t current_label;
	uint16_t current_label_addr;

	buxn_chess_signature_t* current_signature;
	buxn_asm_sym_t signature_tokens[BUXN_CHESS_MAX_SIG_TOKENS];
	uint8_t num_sig_tokens;
	char* tmp_buff_ptr;
	char tmp_buf[BUXN_CHESS_MAX_ARG_LEN * BUXN_CHESS_MAX_SIG_TOKENS];
	buxn_chess_sig_parse_state_t sig_parse_state;
	buxn_chess_addr_map_t addr_map;
	buxn_chess_jump_map_t jump_map;

	buxn_chess_entry_t* entry_pool;
	buxn_chess_entry_t* verification_list;
	bool success;

	buxn_asm_sym_t* last_sym;

	char addr_fmt_buf[1024];

	// Keep this at the end so it is demand paged
	buxn_asm_sym_t* symbols[UINT16_MAX + 1];
};

// https://nullprogram.com/blog/2018/07/31/
static uint32_t
buxn_chess_prospector32(uint32_t x) {
	x ^= x >> 15;
	x *= 0x2c1b3c6dU;
	x ^= x >> 12;
	x *= 0x297a2d39U;
	x ^= x >> 15;
	return x;
}

static buxn_chess_addr_info_t*
buxn_chess_addr_info(buxn_chess_t* chess, uint16_t addr) {
	uint32_t hash = buxn_chess_prospector32(addr);
	buxn_chess_addr_info_t* result;
	BHAMT_GET(chess->addr_map.root, result, hash, addr, BUXN_CHESS_ADDR_EQ);
	return result;
}

static buxn_chess_addr_info_t*
buxn_chess_ensure_addr_info(buxn_chess_t* chess, uint16_t addr) {
	uint32_t hash = buxn_chess_prospector32(addr);
	buxn_chess_addr_info_t** itr;
	buxn_chess_addr_info_t* result;
	BHAMT_SEARCH(chess->addr_map.root, itr, result, hash, addr, BUXN_CHESS_ADDR_EQ);
	if (result == NULL) {
		result = *itr = buxn_chess_alloc(
			chess->ctx,
			sizeof(buxn_chess_addr_info_t),
			_Alignof(buxn_chess_addr_info_t)
		);
		*result = (buxn_chess_addr_info_t){
			.key = addr,
		};
	}

	return result;
}

static void
buxn_chess_report_error(buxn_chess_t* chess, const buxn_asm_report_t* report) {
	buxn_chess_report(chess->ctx, BUXN_ASM_REPORT_ERROR, report);
	chess->success = false;
}

static buxn_chess_entry_t*
buxn_chess_alloc_entry(buxn_chess_t* chess) {
	buxn_chess_entry_t* new_entry;
	if (chess->entry_pool != NULL) {
		new_entry = chess->entry_pool;
		chess->entry_pool = new_entry->next;
	} else {
		new_entry = buxn_chess_alloc(
			chess->ctx,
			sizeof(buxn_chess_entry_t),
			_Alignof(buxn_chess_entry_t)
		);
	}

	return new_entry;
}

static inline void
buxn_chess_add_entry(buxn_chess_entry_t** head, buxn_chess_entry_t* item) {
	item->next = *head;
	*head = item;
}

// String {{{

static const char*
buxn_chess_tmp_strcpy(buxn_chess_t* chess, const char* str) {
	size_t len = strlen(str);
	char* result = chess->tmp_buff_ptr;
	uintptr_t new_ptr = (uintptr_t)chess->tmp_buff_ptr + len + 1;
	uintptr_t end_ptr = (uintptr_t)chess->tmp_buf + sizeof(chess->tmp_buf);
	if (new_ptr > end_ptr) {
		return NULL;
	} else {
		chess->tmp_buff_ptr = (char*)new_ptr;
		memcpy(result, str, len + 1);
		return result;
	}
}

static buxn_chess_str_t
buxn_chess_strcpy(buxn_chess_t* chess, const char* str, size_t len) {
	// TODO: intern
	char* copy = buxn_chess_alloc(chess->ctx, len + 1, _Alignof(char));
	memcpy(copy, str, len);
	copy[len] = '\0';
	return (buxn_chess_str_t){
		.chars = copy,
		.len = (uint8_t)len,
	};
}

static inline buxn_chess_str_t
buxn_chess_vprintf(buxn_chess_t* chess, const char* fmt, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);
	char* chars = buxn_chess_alloc(chess->ctx, len + 1, _Alignof(char));
	vsnprintf(chars, len + 1, fmt, args);

	return (buxn_chess_str_t){
		.chars = chars,
		.len = len,
	};
}

BFORMAT_ATTRIBUTE(2, 3)
static inline buxn_chess_str_t
buxn_chess_printf(buxn_chess_t* chess, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	buxn_chess_str_t result = buxn_chess_vprintf(chess, fmt, args);
	va_end(args);
	return result;
}

static buxn_chess_str_t
buxn_chess_format_value(buxn_chess_t* chess, buxn_chess_value_t value) {
	return buxn_chess_printf(
		chess,
		" %.*s%s",
		(int)value.name.len, value.name.chars,
		(value.semantics & BUXN_CHESS_SEM_SIZE_MASK) == BUXN_CHESS_SEM_SIZE_SHORT
			? "*"
			: ""
	);
}

static buxn_chess_str_t
buxn_chess_format_stack(
	buxn_chess_t* chess,
	const buxn_chess_value_t* value,
	uint8_t len
) {
	buxn_chess_str_t parts[BUXN_CHESS_MAX_ARGS] = { 0 };
	uint8_t print_len = len <= BUXN_CHESS_MAX_ARGS ? len : BUXN_CHESS_MAX_ARGS;
	for (uint8_t i = 0; i < print_len; ++i) {
		parts[i] = buxn_chess_format_value(chess, value[i]);
	}
	buxn_chess_str_t str = buxn_chess_printf(
		chess,
		"%.*s%.*s%.*s%.*s%.*s%.*s%.*s%.*s%s",
		(int)parts[0].len, parts[0].chars,
		(int)parts[1].len, parts[1].chars,
		(int)parts[2].len, parts[2].chars,
		(int)parts[3].len, parts[3].chars,
		(int)parts[4].len, parts[4].chars,
		(int)parts[5].len, parts[5].chars,
		(int)parts[6].len, parts[6].chars,
		(int)parts[7].len, parts[7].chars,
		len > BUXN_CHESS_MAX_ARGS ? " ..." : ""
	);
	return str;
}

static buxn_chess_str_t
buxn_chess_name_prime(buxn_chess_t* chess, buxn_chess_str_t name) {
	return buxn_chess_printf(chess, "%.*s′", (int)name.len, name.chars);
}

static buxn_chess_str_t
buxn_chess_name_binary(buxn_chess_t* chess, buxn_chess_str_t lhs, buxn_chess_str_t rhs) {
	return buxn_chess_printf(
		chess,
		"%.*s·%.*s",
		(int)lhs.len, lhs.chars,
		(int)rhs.len, rhs.chars
	);
}

static const char*
buxn_chess_format_address(buxn_chess_t* chess, uint16_t addr) {
	const buxn_asm_sym_t* sym = chess->symbols[addr];
	if (sym == NULL) {
		snprintf(chess->addr_fmt_buf, sizeof(chess->addr_fmt_buf), "0x%04x", addr);
	} else {
		snprintf(
			chess->addr_fmt_buf,
			sizeof(chess->addr_fmt_buf),
			"%s:%d:%d",
			sym->region.filename,
			sym->region.range.start.line, sym->region.range.start.col
		);
	}
	return chess->addr_fmt_buf;
}

static buxn_chess_str_t
buxn_chess_name_from_symbol(
	buxn_chess_t* chess,
	const buxn_asm_sym_t* sym,
	uint16_t value
) {
	if (sym != NULL && sym->name != NULL) {
		return (buxn_chess_str_t){
			// symbol names are interned so they can just be quoted
			.chars = sym->name,
			.len = strlen(sym->name),
		};
	} else {
		return buxn_chess_printf(chess, "load@0x%04x", value);
	}
}

// }}}

// Symbolic execution {{{

static uint8_t
buxn_chess_value_size(buxn_chess_value_t value) {
	return (value.semantics & BUXN_CHESS_SEM_SIZE_MASK) == BUXN_CHESS_SEM_SIZE_SHORT ? 2 : 1;
}

static inline void
buxn_chess_terminate(buxn_chess_exec_ctx_t* ctx) {
	ctx->terminated = true;
}

static void
buxn_chess_maybe_report_exec_begin(
	buxn_chess_exec_ctx_t* ctx
) {
	if (ctx->entry_reported) { return; }

	void* region = buxn_chess_begin_mem_region(ctx->chess->ctx);
	buxn_chess_str_t init_wst_str = buxn_chess_format_stack(
		ctx->chess,
		ctx->init_wst.content, ctx->init_wst.len
	);
	buxn_chess_str_t init_rst_str;
	if (ctx->entry->info->value.signature->type == BUXN_CHESS_SUBROUTINE) {
		// Exclude the implicit return address
		init_rst_str = buxn_chess_format_stack(
			ctx->chess,
			ctx->init_rst.content + 1, ctx->init_rst.len - 1
		);
	} else {
		init_rst_str = buxn_chess_format_stack(
			ctx->chess,
			ctx->init_rst.content, ctx->init_rst.len
		);
	}

	buxn_chess_report(
		ctx->chess->ctx,
		BUXN_ASM_REPORT_WARNING,
		&(buxn_asm_report_t){
			.message = buxn_chess_printf(
				ctx->chess,
				"Found issues with %.*s starting with (%.*s .%.*s ) from here",
				(int)ctx->entry->info->value.name.len, ctx->entry->info->value.name.chars,
				(int)init_wst_str.len, init_wst_str.chars,
				(int)init_rst_str.len, init_rst_str.chars
			).chars,
			.region = &ctx->start_sym->region,
		}
	);
	buxn_chess_end_mem_region(ctx->chess->ctx, region);
	ctx->entry_reported = true;
}

BFORMAT_ATTRIBUTE(2, 3)
static void
buxn_chess_report_exec_error(
	buxn_chess_exec_ctx_t* ctx,
	const char* fmt,
	...
) {
	buxn_chess_maybe_report_exec_begin(ctx);
	void* region = buxn_chess_begin_mem_region(ctx->chess->ctx);
	va_list args;
	va_start(args, fmt);
	buxn_chess_report_error(ctx->chess, &(buxn_asm_report_t){
		.message = buxn_chess_vprintf(ctx->chess, fmt, args).chars,
		.region = &ctx->current_sym->region,
	});
	va_end(args);
	buxn_chess_end_mem_region(ctx->chess->ctx, region);
	ctx->error_region = ctx->current_sym->region;
	buxn_chess_terminate(ctx);
}

BFORMAT_ATTRIBUTE(2, 3)
static void
buxn_chess_report_exec_warning(
	buxn_chess_exec_ctx_t* ctx,
	const char* fmt,
	...
) {
	buxn_chess_maybe_report_exec_begin(ctx);
	void* region = buxn_chess_begin_mem_region(ctx->chess->ctx);
	va_list args;
	va_start(args, fmt);
	buxn_chess_report(
		ctx->chess->ctx,
		BUXN_ASM_REPORT_WARNING,
		&(buxn_asm_report_t){
			.message = buxn_chess_vprintf(ctx->chess, fmt, args).chars,
			.region = &ctx->current_sym->region,
		}
	);
	va_end(args);
	buxn_chess_end_mem_region(ctx->chess->ctx, region);
}

static void
buxn_chess_raw_push(
	buxn_chess_stack_t* stack,
	buxn_chess_value_t value
) {
	stack->content[stack->len] = value;
	stack->len += 1;
	stack->size += buxn_chess_value_size(value);
}

static void
buxn_chess_queue_routine(buxn_chess_t* chess, buxn_chess_addr_info_t* routine) {
	if (routine->queued) { return; }

	buxn_chess_entry_t* entry = buxn_chess_alloc_entry(chess);
	*entry = (buxn_chess_entry_t){
		.address = routine->key,
		.info = routine,
	};
	buxn_chess_signature_t* signature = routine->value.signature;
	for (uint8_t i = 0; i < signature->wst_in.len; ++i) {
		buxn_chess_raw_push(&entry->state.wst, signature->wst_in.content[i]);
	}

	// A subroutine expects a return address in the return stack
	if (signature->type == BUXN_CHESS_SUBROUTINE) {
		buxn_chess_raw_push(&entry->state.rst, (buxn_chess_value_t){
			.name = {
				.chars = "@return-root",
				.len = BLIT_STRLEN("@return-root"),
			},
			.semantics = BUXN_CHESS_SEM_SIZE_SHORT | BUXN_CHESS_SEM_ADDRESS | BUXN_CHESS_SEM_RETURN,
		});
	}
	for (uint8_t i = 0; i < signature->rst_in.len; ++i) {
		buxn_chess_raw_push(&entry->state.rst, signature->rst_in.content[i]);
	}

	buxn_chess_add_entry(&chess->verification_list, entry);
	BUXN_CHESS_DEBUG(
		"Queued %.*s at %s",
		(int)entry->info->value.name.len, entry->info->value.name.chars,
		buxn_chess_format_address(chess, entry->address)
	);
	routine->queued = true;
}

static inline bool
buxn_chess_op_flag_2(buxn_chess_exec_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_CHESS_OP_2;
}

static inline bool
buxn_chess_op_flag_k(buxn_chess_exec_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_CHESS_OP_K;
}

static inline bool
buxn_chess_op_flag_r(buxn_chess_exec_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_CHESS_OP_R;
}

static inline buxn_chess_value_t
buxn_chess_value_error(void) {
	return (buxn_chess_value_t){
		.name = {
			.chars = "(error)",
			.len = BLIT_STRLEN("(error)")
		}
	};
}

static buxn_chess_value_t
buxn_chess_pop_from(buxn_chess_exec_ctx_t* ctx, buxn_chess_stack_t* stack, uint8_t size) {
	if (size > stack->size) {
		buxn_chess_report_exec_error(ctx, "Stack underflow");
		return buxn_chess_value_error();
	}

	buxn_chess_value_t top = stack->content[stack->len - 1];
	uint8_t top_size = buxn_chess_value_size(top);
	if (top_size == size) {  // Exact size match
		stack->len -= 1;
		stack->size -= top_size;
		return top;
	} else if (top_size > size) {  // Break the top value into hi and lo
		// TODO: handle breaking label address
		buxn_chess_value_t lo = {
			.name = buxn_chess_printf(
				ctx->chess,
				"%.*s-lo",
				(int)top.name.len, top.name.chars
			),
		};
		buxn_chess_value_t hi = {
			.name = buxn_chess_printf(
				ctx->chess,
				"%.*s-hi",
				(int)top.name.len, top.name.chars
			),
		};
		if (top.semantics & BUXN_CHESS_SEM_CONST) {
			lo.semantics |= BUXN_CHESS_SEM_CONST;
			lo.value = top.value & 0xff;
			hi.semantics |= BUXN_CHESS_SEM_CONST;
			hi.value = top.value >> 8;
		}

		stack->content[stack->len - 1] = hi;
		stack->size -= 1;
		return lo;
	} else /* if (top_size < size) */ {  // Merge the top value with the next value's lo part
		// This must be possible without underflow since we already checked size
		assert(stack->len >= 2 && "Stack underflow");
		buxn_chess_value_t lo = buxn_chess_pop_from(ctx, stack, 1);
		buxn_chess_value_t hi = buxn_chess_pop_from(ctx, stack, 1);

		buxn_chess_value_t result = {
			.name = buxn_chess_name_binary(ctx->chess, hi.name, lo.name),
			.semantics = BUXN_CHESS_SEM_SIZE_SHORT,
		};
		if (
			(hi.semantics & BUXN_CHESS_SEM_CONST)
			&& (lo.semantics & BUXN_CHESS_SEM_CONST)
		) {
			result.semantics |= BUXN_CHESS_SEM_CONST;
			result.value = (hi.value << 8) | lo.value;
		}

		return result;
	}
}

static buxn_chess_value_t
buxn_chess_pop_ex(buxn_chess_exec_ctx_t* ctx, bool flag_2, bool flag_r) {
	if (ctx->terminated) { return buxn_chess_value_error(); }

	return buxn_chess_pop_from(
		ctx,
		// Pop goes through a pointer as it can target the shadow stack
		flag_r ? ctx->rsp : ctx->wsp,
		flag_2 ? 2 : 1
	);
}

static buxn_chess_value_t
buxn_chess_pop(buxn_chess_exec_ctx_t* ctx) {
	return buxn_chess_pop_ex(
		ctx,
		buxn_chess_op_flag_2(ctx),
		buxn_chess_op_flag_r(ctx)
	);
}

static void
buxn_chess_push_ex(buxn_chess_exec_ctx_t* ctx, bool flag_r, buxn_chess_value_t value) {
	if (ctx->terminated) { return; }

	uint8_t value_size = buxn_chess_value_size(value);
	// Push is always applied directly to the real stack
	buxn_chess_stack_t* stack = flag_r ? &ctx->entry->state.rst : &ctx->entry->state.wst;
	if ((int)stack->size + (int)value_size > 256) {
		buxn_chess_report_exec_error(ctx, "Stack overflow");
	} else {
		stack->size += value_size;
		stack->content[stack->len++] = value;
	}
}

static void
buxn_chess_push(buxn_chess_exec_ctx_t* ctx, buxn_chess_value_t value) {
	buxn_chess_push_ex(ctx, buxn_chess_op_flag_r(ctx), value);
}

typedef enum {
	BUXN_CHESS_CHECK_STACK_EXACT,
	BUXN_CHESS_CHECK_STACK_AT_LEAST,
} buxn_chess_stack_check_type_t;

static void
buxn_chess_check_stack(
	buxn_chess_exec_ctx_t* ctx,
	buxn_chess_stack_check_type_t check_type,
	const char* stack_name,
	buxn_chess_stack_t* stack,
	const buxn_chess_sig_stack_t* signature
) {
	void* region = buxn_chess_begin_mem_region(ctx->chess->ctx);
	uint8_t sig_size = 0;
	for (uint8_t i = 0; i < signature->len; ++i) {
		sig_size += buxn_chess_value_size(signature->content[i]);
	}

	bool match = false;
	const char* prefix = "";
	switch (check_type) {
		case BUXN_CHESS_CHECK_STACK_EXACT:
			match = stack->size == sig_size;
			break;
		case BUXN_CHESS_CHECK_STACK_AT_LEAST:
			match = stack->size >= sig_size;
			prefix = "at least ";
			break;
	}

	if (!match) {
		buxn_chess_str_t sig_str = buxn_chess_format_stack(
			ctx->chess, signature->content, signature->len
		);
		buxn_chess_str_t stack_str = buxn_chess_format_stack(
			ctx->chess, stack->content, stack->len
		);
		buxn_chess_report_exec_error(
			ctx,
			"%s stack size mismatch: Expecting %s%d (%.*s ), got %d (%.*s )",
			stack_name,
			prefix,
			sig_size, (int)sig_str.len, sig_str.chars,
			stack->size, (int)stack_str.len, stack_str.chars
		);
	}

	if (ctx->terminated) { return; }

	// Type check individual elements
	for (uint8_t i = 0; i < signature->len; ++i) {
		uint8_t value_index = signature->len - 1 - i;
		buxn_chess_value_t sig_value = signature->content[value_index];
		buxn_chess_value_t actual_value = buxn_chess_pop_from(
			ctx,
			stack,
			sig_value.semantics & BUXN_CHESS_SEM_SIZE_SHORT ? 2 : 1
		);

		// Assignability checks

		if (
			(sig_value.semantics & BUXN_CHESS_SEM_ADDRESS)
			&& (
				(actual_value.semantics & BUXN_CHESS_SEM_ADDRESS) == 0
				&&
				(actual_value.semantics & BUXN_CHESS_SEM_CONST) == 0
			)
		) {
			buxn_chess_report_exec_warning(
				ctx,
				"%s stack #%d: An address value (%.*s) is constructed from a value that is not derived from an address or constant (%.*s)",
				stack_name,
				value_index,
				(int)sig_value.name.len, sig_value.name.chars,
				(int)actual_value.name.len, actual_value.name.chars
			);
		}

		if (
			(sig_value.semantics & BUXN_CHESS_SEM_ROUTINE)
			&&
			((actual_value.semantics & BUXN_CHESS_SEM_ROUTINE) == 0)
		) {
			buxn_chess_report_exec_error(
				ctx,
				"%s stack #%d: A routine value (%.*s) cannot be constructed from a non-routine value (%.*s)",
				stack_name,
				value_index,
				(int)sig_value.name.len, sig_value.name.chars,
				(int)actual_value.name.len, actual_value.name.chars
			);
		}

		if (sig_value.semantics & BUXN_CHESS_SEM_NOMINAL) {
			if (!(
				// Nominally-typed value can be constructed from raw value
				(actual_value.semantics & BUXN_CHESS_SEM_NOMINAL) == 0
				||
				// Nominal subtyping by prefix
				// A value of type "Suits/Heart" is assignable to "Suits/"
				(sig_value.type.len <= actual_value.type.len
				 &&
				 memcmp(sig_value.type.chars, actual_value.type.chars, sig_value.type.len) == 0)
			)) {
				buxn_chess_report_exec_error(
					ctx,
					"%s stack #%d: A value of type \"%.*s\" (%.*s) cannot be constructed from a value of type \"%.*s\" (%.*s)",
					stack_name,
					value_index,
					(int)sig_value.type.len, sig_value.type.chars,
					(int)sig_value.name.len, sig_value.name.chars,
					(int)actual_value.type.len, actual_value.type.chars,
					(int)actual_value.name.len, actual_value.name.chars
				);
			}
		}
	}

	buxn_chess_end_mem_region(ctx->chess->ctx, region);
}

static void
buxn_chess_check_return(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_check_stack(
		ctx,
		BUXN_CHESS_CHECK_STACK_EXACT,
		"Output working",
		&ctx->entry->state.wst,
		&ctx->entry->info->value.signature->wst_out
	);
	buxn_chess_check_stack(
		ctx,
		BUXN_CHESS_CHECK_STACK_EXACT,
		"Output return",
		&ctx->entry->state.rst,
		&ctx->entry->info->value.signature->rst_out
	);
}

static buxn_chess_entry_t*
buxn_chess_fork(buxn_chess_exec_ctx_t* ctx) {
	BUXN_CHESS_DEBUG(
		"Forked %.*s at %s",
		(int)ctx->entry->info->value.name.len, ctx->entry->info->value.name.chars,
		buxn_chess_format_address(ctx->chess, ctx->pc)
	);
	buxn_chess_entry_t* new_entry = buxn_chess_alloc_entry(ctx->chess);
	*new_entry = *ctx->entry;
	new_entry->address = ctx->pc;
	new_entry->next = ctx->chess->verification_list;
	ctx->chess->verification_list = new_entry;
	return new_entry;
}

static void
buxn_chess_BRK(buxn_chess_exec_ctx_t* ctx) {
	if (ctx->entry->info->value.signature->type == BUXN_CHESS_SUBROUTINE) {
		buxn_chess_report_exec_error(ctx, "Subroutine called BRK");
	}

	buxn_chess_check_return(ctx);
	buxn_chess_terminate(ctx);
	BUXN_CHESS_DEBUG("Terminated by BRK");
}

static void
buxn_chess_INC(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	if (value.semantics & BUXN_CHESS_SEM_CONST) {
		value.value += 1;
	}
	value.semantics &= ~(BUXN_CHESS_SEM_NOMINAL);
	value.name = buxn_chess_name_prime(ctx->chess, value.name);
	buxn_chess_push(ctx, value);
}

static void
buxn_chess_POP(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_pop(ctx);
}

static void
buxn_chess_NIP(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	(void)a;
	buxn_chess_push(ctx, b);
}

static void
buxn_chess_SWP(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_push(ctx, b);
	buxn_chess_push(ctx, a);
}

static void
buxn_chess_ROT(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t c = buxn_chess_pop(ctx);
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_push(ctx, b);
	buxn_chess_push(ctx, c);
	buxn_chess_push(ctx, a);
}

static void
buxn_chess_DUP(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_push(ctx, a);
	buxn_chess_push(ctx, a);
}

static void
buxn_chess_OVR(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_push(ctx, a);
	buxn_chess_push(ctx, b);
	buxn_chess_push(ctx, a);
}

static inline void
buxn_chess_boolean_op(
	buxn_chess_exec_ctx_t* ctx,
	bool (*op)(uint16_t lhs, uint16_t rhs)
) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_value_t result = {
		.name = buxn_chess_name_binary(ctx->chess, a.name, b.name),
		.semantics = BUXN_CHESS_SEM_SIZE_BYTE,
	};
	if (
		(a.semantics & BUXN_CHESS_SEM_CONST)
		&& (b.semantics & BUXN_CHESS_SEM_CONST)
	) {
		result.semantics |= BUXN_CHESS_SEM_CONST;
		result.value = op(a.value, b.value);
	}
	buxn_chess_push(ctx, result);
}

static inline bool
buxn_chess_bool_equ(uint16_t lhs, uint16_t rhs) {
	return lhs == rhs;
}

static void
buxn_chess_EQU(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_boolean_op(ctx, buxn_chess_bool_equ);
}

static inline bool
buxn_chess_bool_neq(uint16_t lhs, uint16_t rhs) {
	return lhs != rhs;
}

static void
buxn_chess_NEQ(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_boolean_op(ctx, buxn_chess_bool_neq);
}

static inline bool
buxn_chess_bool_gth(uint16_t lhs, uint16_t rhs) {
	return lhs > rhs;
}

static void
buxn_chess_GTH(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_boolean_op(ctx, buxn_chess_bool_gth);
}

static inline bool
buxn_chess_bool_lth(uint16_t lhs, uint16_t rhs) {
	return lhs < rhs;
}

static void
buxn_chess_LTH(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_boolean_op(ctx, buxn_chess_bool_lth);
}

static enum {
	BUXN_CHESS_JUMP_TERMINATED,
	BUXN_CHESS_JUMP_CONTINUE,
	BUXN_CHESS_JUMP_SUBROUTINE,
} buxn_chess_jump(buxn_chess_exec_ctx_t* ctx, buxn_chess_value_t addr) {
	if (ctx->terminated) { return BUXN_CHESS_JUMP_TERMINATED; }

	uint16_t from_pc = ctx->pc;
	if ((addr.semantics & BUXN_CHESS_SEM_SIZE_MASK) == BUXN_CHESS_SEM_SIZE_BYTE) {
		// Relative jump
		if (addr.semantics & BUXN_CHESS_SEM_CONST) {
			ctx->pc = (uint16_t)((int32_t)ctx->pc + (int32_t)(int8_t)addr.value);
		} else {
			buxn_chess_report_exec_error(ctx, "Jumping to unknown address");
			return BUXN_CHESS_JUMP_TERMINATED;
		}
	} else {
		// Absolute jump
		if (
			(addr.semantics & BUXN_CHESS_SEM_ADDRESS)
			&&
			(addr.semantics & BUXN_CHESS_SEM_RETURN)
		) {
			if (ctx->entry->info->value.signature->type == BUXN_CHESS_VECTOR) {
				buxn_chess_report_exec_error(ctx, "Vector routine makes a normal return");
			}
			buxn_chess_check_return(ctx);
			buxn_chess_terminate(ctx);
			BUXN_CHESS_DEBUG("Terminated by jumping to return address");
			return BUXN_CHESS_JUMP_TERMINATED;
		} else if (addr.semantics & BUXN_CHESS_SEM_CONST) {
			ctx->pc = addr.value;
		} else {
			buxn_chess_report_exec_error(ctx, "Jumping to unknown address");
			return BUXN_CHESS_JUMP_TERMINATED;
		}
	}

	buxn_chess_addr_info_t* addr_info = buxn_chess_addr_info(ctx->chess, ctx->pc);
	if (addr_info != NULL && (addr_info->value.semantics & BUXN_CHESS_SEM_ROUTINE)) {
		// The target jump can be short-circuited into just applying the signature effect
		// without jumping
		const buxn_chess_signature_t* sig = addr_info->value.signature;

		if (
			ctx->entry->info->value.signature->type == BUXN_CHESS_SUBROUTINE
			&& sig->type == BUXN_CHESS_VECTOR
		) {
			buxn_chess_report_exec_error(ctx, "Subroutine calls into a vector");
			return BUXN_CHESS_JUMP_TERMINATED;
		}

		// Gather inputs directly from the real stack
		buxn_chess_check_stack(
			ctx,
			BUXN_CHESS_CHECK_STACK_AT_LEAST,
			"Input working",
			&ctx->entry->state.wst,
			&sig->wst_in
		);
		buxn_chess_check_stack(
			ctx,
			BUXN_CHESS_CHECK_STACK_AT_LEAST,
			"Input return",
			&ctx->entry->state.rst,
			&sig->rst_in
		);
		// Push outputs
		for (uint8_t i = 0; i < sig->wst_out.len; ++i) {
			buxn_chess_push_ex(ctx, false, sig->wst_out.content[i]);
		}
		for (uint8_t i = 0; i < sig->rst_out.len; ++i) {
			buxn_chess_push_ex(ctx, true, sig->rst_out.content[i]);
		}

		// Check the subroutine
		buxn_chess_queue_routine(ctx->chess, addr_info);
		return BUXN_CHESS_JUMP_SUBROUTINE;
	} else {
		// Make the jump if it was not made before
		// The first time a backward jump is applied, the effect of the loop
		// body is already applied once
		// Applying it just one more time is enough to confirm that it is
		// idempotent (i.e: f(f(x)) == f(x))
		uint32_t jump_key = ((uint32_t)from_pc << 16) | (uint32_t)ctx->pc;
		uint32_t jump_hash = buxn_chess_prospector32(jump_key);
		buxn_chess_jump_arc_t** itr;
		buxn_chess_jump_arc_t* jump_node;
		BHAMT_SEARCH(
			ctx->chess->jump_map.root,
			itr, jump_node,
			jump_hash, jump_key,
			BUXN_CHESS_ADDR_EQ
		);
		if (jump_node == NULL) {
			*itr = jump_node = buxn_chess_alloc(
				ctx->chess->ctx,
				sizeof(buxn_chess_jump_arc_t),
				_Alignof(buxn_chess_jump_arc_t)
			);
			*jump_node = (buxn_chess_jump_arc_t){ .key = jump_key };
			return BUXN_CHESS_JUMP_CONTINUE;
		} else {
			BUXN_CHESS_DEBUG(
				"Terminated by repeated jump to %s",
				buxn_chess_format_address(ctx->chess, ctx->pc)
			);
			buxn_chess_terminate(ctx);
			return BUXN_CHESS_JUMP_TERMINATED;
		}
	}
}

static void
buxn_chess_jump_no_return(buxn_chess_exec_ctx_t* ctx, buxn_chess_value_t addr) {
	if (buxn_chess_jump(ctx, addr) == BUXN_CHESS_JUMP_SUBROUTINE) {
		// This jump was short-circuited
		// Check return type and terminate
		if (ctx->entry->info->value.signature->type == BUXN_CHESS_VECTOR) {
			buxn_chess_report_exec_error(ctx, "Vector routine makes a normal return");
			return;
		}

		if (ctx->entry->state.rst.len != 1)  {
			buxn_chess_report_exec_error(ctx, "Invalid return stack, expecting only return address");
			return;
		}

		buxn_chess_value_t return_addr = buxn_chess_pop_ex(ctx, true, true);
		if (!(
			(return_addr.semantics & BUXN_CHESS_SEM_ADDRESS)
			&&
			(return_addr.semantics & BUXN_CHESS_SEM_RETURN)
		)) {
			buxn_chess_report_exec_error(ctx, "Invalid return stack, expecting only return address");
			return;
		}

		buxn_chess_check_return(ctx);
		buxn_chess_terminate(ctx);
		BUXN_CHESS_DEBUG("Terminated by subroutine tail call");
	}
}

static void
buxn_chess_jump_stash(
	buxn_chess_exec_ctx_t* ctx,
	buxn_chess_value_t addr,
	bool flag_r
) {
	buxn_chess_value_t pc = {
		.name = {
			.chars = "@return-sub",
			.len = BLIT_STRLEN("@return-sub"),
		},
		.semantics = BUXN_CHESS_SEM_SIZE_SHORT | BUXN_CHESS_SEM_ADDRESS | BUXN_CHESS_SEM_CONST,
		.value = ctx->pc,
	};
	buxn_chess_push_ex(ctx, !flag_r, pc);
	if (buxn_chess_jump(ctx, addr) == BUXN_CHESS_JUMP_SUBROUTINE) {
		// This jump was short-circuited
		// Continue from the saved pc
		buxn_chess_pop_from(  // The short-circuit code does not pop
			ctx,
			// The stacks are reversed
			flag_r ? &ctx->entry->state.wst : &ctx->entry->state.rst,
			2
		);
		ctx->pc = pc.value;
	}
}

static void
buxn_chess_JMP(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop(ctx);
	buxn_chess_jump_no_return(ctx, addr);
}

static void
buxn_chess_JCN(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop(ctx);
	buxn_chess_value_t cond = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	(void)cond;

	// We should not check cond for const-ness
	// Since we only run two iterations of a numeric loop, if the inputs are
	// constant, we will never fork a branch that exits the loop
	buxn_chess_fork(ctx);  // False branch
	buxn_chess_jump_no_return(ctx, addr);  // True branch
}

static void
buxn_chess_JSR(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop(ctx);
	buxn_chess_jump_stash(ctx, addr, buxn_chess_op_flag_r(ctx));
}

static void
buxn_chess_STH(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_push_ex(ctx, !buxn_chess_op_flag_r(ctx), a);
}

static void
buxn_chess_load(buxn_chess_exec_ctx_t* ctx, buxn_chess_value_t addr) {
	if (ctx->terminated) { return; }

	if (
		(addr.semantics & BUXN_CHESS_SEM_ADDRESS) == 0
		&&
		(addr.semantics & BUXN_CHESS_SEM_CONST) == 0
	) {
		buxn_chess_report_exec_warning(
			ctx,
			"Load address (%.*s) is not a constant or an offset of one",
			(int)addr.name.len, addr.name.chars
		);
	}

	buxn_chess_value_t value = { 0 };

	if (addr.semantics & BUXN_CHESS_SEM_CONST) {
		buxn_chess_addr_info_t* addr_info = buxn_chess_addr_info(ctx->chess, addr.value);
		if (addr_info != NULL) {  // Load from label
			value = addr_info->value;
			value.name = buxn_chess_printf(
				ctx->chess,
				"load@%.*s",
				(int)addr_info->value.name.len,
				addr_info->value.name.chars
			);
		} else {
			const buxn_asm_sym_t* symbol = ctx->chess->symbols[addr.value];
			if (symbol != NULL && symbol->type == BUXN_ASM_SYM_OPCODE) {
				buxn_chess_report_exec_warning(
					ctx,
					"Load address (%.*s) points to an executable region",
					(int)addr.name.len, addr.name.chars
				);
			}
			value.name = buxn_chess_printf(ctx->chess, "load@0x%04x", ctx->pc);
		}
	} else {
		value.name = buxn_chess_printf(ctx->chess, "load@0x%04x", ctx->pc);
	}

	value.semantics |= buxn_chess_op_flag_2(ctx)
		? BUXN_CHESS_SEM_SIZE_SHORT
		: BUXN_CHESS_SEM_SIZE_BYTE;
	buxn_chess_push(ctx, value);
}

static void
buxn_chess_store(
	buxn_chess_exec_ctx_t* ctx,
	buxn_chess_value_t addr,
	buxn_chess_value_t value
) {
	if (ctx->terminated) { return; }

	if (
		(addr.semantics & BUXN_CHESS_SEM_ADDRESS) == 0
		&&
		(addr.semantics & BUXN_CHESS_SEM_CONST) == 0
	) {
		buxn_chess_report_exec_warning(
			ctx,
			"Store address (%.*s) is not a constant or an offset of one",
			(int)addr.name.len, addr.name.chars
		);
	}

	if (addr.semantics & BUXN_CHESS_SEM_CONST) {
		buxn_chess_addr_info_t* addr_info = buxn_chess_addr_info(ctx->chess, addr.value);
		if (addr_info != NULL) {  // Store to label
			(void)addr_info;
			(void)value;
			// TODO: checked store
		} else {
			const buxn_asm_sym_t* symbol = ctx->chess->symbols[addr.value];
			if (symbol != NULL && symbol->type == BUXN_ASM_SYM_OPCODE) {
				buxn_chess_report_exec_warning(
					ctx,
					"Store address (%.*s) points to an executable region",
					(int)addr.name.len, addr.name.chars
				);
			}
		}
	}
}

static void
buxn_chess_LDZ(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_load(ctx, addr);
}

static void
buxn_chess_STZ(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	buxn_chess_store(ctx, addr, value);
}

static void
buxn_chess_LDR(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	if (addr.semantics & BUXN_CHESS_SEM_CONST) {
		addr.value = (uint16_t)((int32_t)ctx->pc + (int32_t)(int8_t)addr.value);
	}
	buxn_chess_load(ctx, addr);
}

static void
buxn_chess_STR(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	if (addr.semantics & BUXN_CHESS_SEM_CONST) {
		addr.value = (uint16_t)((int32_t)ctx->pc + (int32_t)(int8_t)addr.value);
	}
	buxn_chess_store(ctx, addr, value);
}

static void
buxn_chess_LDA(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, true, buxn_chess_op_flag_r(ctx));
	buxn_chess_load(ctx, addr);
}

static void
buxn_chess_STA(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, true, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	buxn_chess_store(ctx, addr, value);
}

static void
buxn_chess_DEI(buxn_chess_exec_ctx_t* ctx) {
	// TODO: Device layout annotation
	// TODO: Check that label is device address
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = {
		.name = buxn_chess_printf(
			ctx->chess, "dei@%.*s", (int)addr.name.len, addr.name.chars
		),
	};

	if ((addr.semantics & BUXN_CHESS_SEM_ADDRESS) == 0) {
		buxn_chess_report_exec_warning(ctx, "DEI from non-label");
	}

	// TODO: Use device layout to figure out the correct size to read
	// Start from the label and scan forward until another label of the same type is encountered
	// A device can only be confined in a 16-byte range
	value.semantics = buxn_chess_op_flag_2(ctx)
		? BUXN_CHESS_SEM_SIZE_SHORT
		: BUXN_CHESS_SEM_SIZE_BYTE;
	buxn_chess_push(ctx, value);
}

static void
buxn_chess_DEO(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	(void)value;

	if ((addr.semantics & BUXN_CHESS_SEM_ADDRESS) == 0) {
		buxn_chess_report_exec_warning(ctx, "DEO from non-label");
	}
}

static void
buxn_chess_bin_op(
	buxn_chess_exec_ctx_t* ctx,
	uint16_t (*op)(uint16_t lhs, uint16_t rhs)
) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_value_t result = {
		.name = buxn_chess_name_binary(ctx->chess, a.name, b.name),
		.semantics = buxn_chess_op_flag_2(ctx)
			? BUXN_CHESS_SEM_SIZE_SHORT
			: BUXN_CHESS_SEM_SIZE_BYTE,
	};
	if (
		(a.semantics & BUXN_CHESS_SEM_CONST)
		&& (b.semantics & BUXN_CHESS_SEM_CONST)
	) {
		result.semantics |= BUXN_CHESS_SEM_CONST;
		result.value = op(a.value, b.value);
	}
	buxn_chess_push(ctx, result);
}

static void
buxn_chess_bin_pointer_arith(
	buxn_chess_exec_ctx_t* ctx,
	uint16_t (*op)(uint16_t lhs, uint16_t rhs)
) {
	buxn_chess_value_t b = buxn_chess_pop(ctx);
	buxn_chess_value_t a = buxn_chess_pop(ctx);
	buxn_chess_value_t result = {
		.name = buxn_chess_name_binary(ctx->chess, a.name, b.name),
		.semantics = buxn_chess_op_flag_2(ctx)
			? BUXN_CHESS_SEM_SIZE_SHORT
			: BUXN_CHESS_SEM_SIZE_BYTE,
	};
	if (
		(a.semantics & BUXN_CHESS_SEM_CONST)
		&& (b.semantics & BUXN_CHESS_SEM_CONST)
	) {
		result.semantics |= BUXN_CHESS_SEM_CONST;
		result.value = op(a.value, b.value);
	}

	// Preserve address trait
	if (
		   (a.semantics & BUXN_CHESS_SEM_ADDRESS)
		|| (b.semantics & BUXN_CHESS_SEM_ADDRESS)
	) {
		result.semantics |= BUXN_CHESS_SEM_ADDRESS;
	}

	buxn_chess_push(ctx, result);
}

static inline uint16_t
buxn_chess_op_add(uint16_t lhs, uint16_t rhs) {
	return lhs + rhs;
}

static void
buxn_chess_ADD(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_pointer_arith(ctx, buxn_chess_op_add);
}

static inline uint16_t
buxn_chess_op_sub(uint16_t lhs, uint16_t rhs) {
	return lhs - rhs;
}

static void
buxn_chess_SUB(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_pointer_arith(ctx, buxn_chess_op_sub);
}

static inline uint16_t
buxn_chess_op_mul(uint16_t lhs, uint16_t rhs) {
	return lhs * rhs;
}

static void
buxn_chess_MUL(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_op(ctx, buxn_chess_op_mul);
}

static inline uint16_t
buxn_chess_op_div(uint16_t lhs, uint16_t rhs) {
	return rhs == 0 ? 0 : lhs / rhs;
}

static void
buxn_chess_DIV(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_op(ctx, buxn_chess_op_div);
}

static inline uint16_t
buxn_chess_op_and(uint16_t lhs, uint16_t rhs) {
	return lhs & rhs;
}

static void
buxn_chess_AND(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_op(ctx, buxn_chess_op_and);
}

static inline uint16_t
buxn_chess_op_or(uint16_t lhs, uint16_t rhs) {
	return lhs | rhs;
}

static void
buxn_chess_ORA(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_op(ctx, buxn_chess_op_or);
}

static inline uint16_t
buxn_chess_op_xor(uint16_t lhs, uint16_t rhs) {
	return lhs ^ rhs;
}

static void
buxn_chess_EOR(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_bin_op(ctx, buxn_chess_op_xor);
}

static void
buxn_chess_SFT(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t shift = buxn_chess_pop_ex(ctx, false, buxn_chess_op_flag_r(ctx));
	buxn_chess_value_t value = buxn_chess_pop(ctx);
	buxn_chess_value_t result = {
		.name = buxn_chess_name_binary(ctx->chess, value.name, shift.name),
		.semantics = buxn_chess_op_flag_2(ctx)
			? BUXN_CHESS_SEM_SIZE_SHORT
			: BUXN_CHESS_SEM_SIZE_BYTE,
	};
	if (
		(value.semantics & BUXN_CHESS_SEM_CONST)
		&& (shift.semantics & BUXN_CHESS_SEM_CONST)
	) {
		result.semantics |= BUXN_CHESS_SEM_CONST;
		result.value = (value.value >> (shift.value & 0x0f)) << ((shift.value & 0xf0) >> 4);
	}
	buxn_chess_push(ctx, result);
}

static buxn_chess_value_t
buxn_chess_immediate_jump_target(buxn_chess_exec_ctx_t* ctx) {
	uint16_t target_addr_hi = ctx->pc++;
	uint16_t target_addr_lo = ctx->pc++;
	buxn_asm_sym_t* symbol_hi = ctx->chess->symbols[target_addr_hi];
	buxn_asm_sym_t* symbol_lo = ctx->chess->symbols[target_addr_lo];

	if (
		symbol_hi == symbol_lo
		&& symbol_hi != NULL
		&& symbol_hi->type == BUXN_ASM_SYM_LABEL_REF
	) {
		uint16_t distant =
			(buxn_chess_get_rom(ctx->chess->ctx, target_addr_hi) << 8)
			|(buxn_chess_get_rom(ctx->chess->ctx, target_addr_lo) << 0);
		return (buxn_chess_value_t){
			.name = buxn_chess_name_from_symbol(ctx->chess, symbol_hi, ctx->pc + distant),
			.semantics = BUXN_CHESS_SEM_SIZE_SHORT | BUXN_CHESS_SEM_ADDRESS | BUXN_CHESS_SEM_CONST,
			.value = ctx->pc + distant,
		};
	} else {
		buxn_chess_report_exec_error(ctx, "Invalid jump address");
		return buxn_chess_value_error();
	}
}

static void
buxn_chess_JCI(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_immediate_jump_target(ctx);
	buxn_chess_value_t cond = buxn_chess_pop_ex(ctx, false, false);
	(void)cond;

	// We should not check cond for const-ness
	// Since we only run two iterations of a numeric loop, if the inputs are
	// constant, we will never fork a branch that exits the loop
	buxn_chess_fork(ctx);  // False branch
	buxn_chess_jump_no_return(ctx, addr);  // True branch
}

static void
buxn_chess_JMI(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_immediate_jump_target(ctx);
	buxn_chess_jump_no_return(ctx, addr);
}

static void
buxn_chess_JSI(buxn_chess_exec_ctx_t* ctx) {
	buxn_chess_value_t addr = buxn_chess_immediate_jump_target(ctx);
	buxn_chess_jump_stash(ctx, addr, false);
}

static buxn_chess_value_t
buxn_chess_make_lit_byte(
	buxn_chess_exec_ctx_t* ctx,
	uint16_t addr,
	const buxn_asm_sym_t* symbol
) {
	buxn_chess_value_t value = { 0 };
	if (symbol == NULL) {
		buxn_chess_report_exec_warning(ctx, "Loading unlabelled literal");
		value.name = buxn_chess_printf(ctx->chess, "lit@0x%04x", addr);
	} else if (symbol->type == BUXN_ASM_SYM_LABEL_REF) {
		// TODO: copy label annotations
		value.semantics |= BUXN_CHESS_SEM_ADDRESS | BUXN_CHESS_SEM_CONST;
		value.value = buxn_chess_get_rom(ctx->chess->ctx, addr);
		value.name = buxn_chess_name_from_symbol(ctx->chess, symbol, value.value);
	} else {
		value.value = buxn_chess_get_rom(ctx->chess->ctx, addr);
		value.name = buxn_chess_printf(ctx->chess, "0x%02x", value.value);
		value.semantics |= BUXN_CHESS_SEM_CONST;
	}

	return value;
}

static void
buxn_chess_LIT(buxn_chess_exec_ctx_t* ctx) {
	if (!buxn_chess_op_flag_2(ctx)) {
		uint16_t lit_addr = ctx->pc++;
		const buxn_asm_sym_t* symbol = ctx->chess->symbols[lit_addr];
		buxn_chess_addr_info_t* addr_info = buxn_chess_addr_info(ctx->chess, lit_addr);
		buxn_chess_value_t value = { 0 };

		if (addr_info != NULL) {  // Door
			value = addr_info->value;
			value.semantics &= ~BUXN_CHESS_SEM_SIZE_SHORT;
		} else if (symbol != NULL) {
			value = buxn_chess_make_lit_byte(ctx, lit_addr, symbol);
		}

		buxn_chess_push(ctx, value);
	} else {
		uint16_t lit_addr_hi = ctx->pc++;
		uint16_t lit_addr_lo = ctx->pc++;
		buxn_asm_sym_t* symbol_hi = ctx->chess->symbols[lit_addr_hi];
		buxn_asm_sym_t* symbol_lo = ctx->chess->symbols[lit_addr_lo];
		buxn_chess_addr_info_t* addr_info_hi = buxn_chess_addr_info(ctx->chess, lit_addr_hi);
		buxn_chess_addr_info_t* addr_info_lo = buxn_chess_addr_info(ctx->chess, lit_addr_lo);

		if (addr_info_hi != NULL) {  // Door
			if (addr_info_lo == NULL && symbol_lo == NULL) {
				buxn_chess_value_t value = addr_info_hi->value;
				value.semantics |= BUXN_CHESS_SEM_SIZE_SHORT;
				buxn_chess_push(ctx, value);
			} else if (addr_info_lo != NULL) {
				buxn_chess_value_t value_hi = addr_info_hi->value;
				value_hi.semantics &= ~BUXN_CHESS_SEM_SIZE_SHORT;
				buxn_chess_push(ctx, value_hi);

				buxn_chess_value_t value_lo = addr_info_lo->value;
				value_lo.semantics &= ~BUXN_CHESS_SEM_SIZE_SHORT;
				buxn_chess_push(ctx, value_lo);
			} else /* if (symbol_lo != NULL) */ {
				buxn_chess_value_t value_hi = addr_info_hi->value;
				value_hi.semantics &= ~BUXN_CHESS_SEM_SIZE_SHORT;
				buxn_chess_push(ctx, value_hi);

				buxn_chess_push(
					ctx,
					buxn_chess_make_lit_byte(ctx, lit_addr_lo, symbol_lo)
				);
			}
		} else if (symbol_hi == symbol_lo) {  // The same symbol
			if (symbol_hi != NULL) {
				buxn_chess_value_t value = {
					.semantics = BUXN_CHESS_SEM_SIZE_SHORT,
				};
				if (symbol_hi->type == BUXN_ASM_SYM_LABEL_REF) {
					// TODO: copy label annotations
					value.semantics |= BUXN_CHESS_SEM_ADDRESS | BUXN_CHESS_SEM_CONST;
					value.value =
						  (buxn_chess_get_rom(ctx->chess->ctx, lit_addr_hi) << 8)
						| (buxn_chess_get_rom(ctx->chess->ctx, lit_addr_lo) << 0);
					value.name = buxn_chess_name_from_symbol(ctx->chess, symbol_hi, value.value);
				} else if (
					symbol_hi->type == BUXN_ASM_SYM_NUMBER
					// TODO: introduce annotation for instruction quoting
					|| symbol_hi->type == BUXN_ASM_SYM_OPCODE  // Quoting instruction
				) {
					value.value =
						  (buxn_chess_get_rom(ctx->chess->ctx, lit_addr_hi) << 8)
						| (buxn_chess_get_rom(ctx->chess->ctx, lit_addr_lo) << 0);
					value.name = buxn_chess_printf(ctx->chess, "0x%04x", value.value);
					value.semantics |= BUXN_CHESS_SEM_CONST;
				} else {
					buxn_chess_report_exec_warning(ctx, "Loading unlabelled literal");
					value.name = buxn_chess_printf(ctx->chess, "lit@0x%04x", lit_addr_hi);
				}
				buxn_chess_push(ctx, value);
			} else {
				buxn_chess_report_exec_warning(ctx, "Loading unlabelled literal");
				buxn_chess_value_t value = {
					.name = buxn_chess_printf(ctx->chess, "lit@0x%04x", lit_addr_hi),
					.semantics = BUXN_CHESS_SEM_SIZE_SHORT,
				};
				buxn_chess_push(ctx, value);
			}
		} else {  // 2 unrelated symbols
			buxn_chess_push(
				ctx,
				buxn_chess_make_lit_byte(ctx, lit_addr_hi, symbol_hi)
			);
			buxn_chess_push(
				ctx,
				buxn_chess_make_lit_byte(ctx, lit_addr_lo, symbol_lo)
			);
		}
	}
}

static void
buxn_chess_copy_stack(buxn_chess_stack_t* dst, const buxn_chess_stack_t* src) {
	memcpy(dst->content, src->content, sizeof(src->content[0]) * src->len);
	dst->len = src->len;
	dst->size = src->size;
}

#undef BUXN_OPCODE_NAME
#define BUXN_OPCODE_NAME(NAME, K, R, S) NAME
#define BUXN_CHESS_DISPATCH(NAME, VALUE) \
	case VALUE: BUXN_CONCAT(buxn_chess_, NAME)(&ctx); break;

static void
buxn_chess_execute(buxn_chess_t* chess, buxn_chess_entry_t* entry) {
	BUXN_CHESS_DEBUG(
		"Analyzing %.*s from %s",
		(int)entry->info->value.name.len, entry->info->value.name.chars,
		buxn_chess_format_address(chess, entry->address)
	);

	buxn_chess_exec_ctx_t ctx = {
		.chess = chess,
		.entry = entry,
		.pc = entry->address,
	};
	void* region = buxn_chess_begin_mem_region(chess->ctx);
	BUXN_CHESS_DEBUG(
		"WST(%d):%s",
		ctx.entry->state.wst.len,
		buxn_chess_format_stack(chess, ctx.entry->state.wst.content, ctx.entry->state.wst.len).chars
	);
	BUXN_CHESS_DEBUG(
		"RST(%d):%s",
		ctx.entry->state.rst.len,
		buxn_chess_format_stack(chess, ctx.entry->state.rst.content, ctx.entry->state.rst.len).chars
	);
	buxn_chess_end_mem_region(chess->ctx, region);

	buxn_chess_stack_t shadow_wst;
	buxn_chess_stack_t shadow_rst;
	buxn_chess_stack_t saved_wst = { .len = 0, .size = 0 };
	buxn_chess_stack_t saved_rst = { .len = 0, .size = 0 };
	buxn_chess_copy_stack(&ctx.init_wst, &ctx.entry->state.wst);
	buxn_chess_copy_stack(&ctx.init_rst, &ctx.entry->state.rst);

	ctx.start_sym = chess->symbols[ctx.pc];
	ctx.current_sym = ctx.start_sym;

	while (!ctx.terminated) {
		if (ctx.pc < 256) {
			buxn_chess_report_exec_error(&ctx, "Execution reached zero page");
			return;
		}

		uint16_t pc = ctx.pc++;
		const buxn_asm_sym_t* current_sym = chess->symbols[pc];
		ctx.current_opcode = buxn_chess_get_rom(chess->ctx, pc);
		BUXN_CHESS_DEBUG(
			"Executing %s at %s",
			buxn_chess_opcode_names[ctx.current_opcode],
			buxn_chess_format_address(chess, pc)
		);
		if (current_sym == NULL || current_sym->type != BUXN_ASM_SYM_OPCODE) {
			buxn_chess_report_exec_error(&ctx, "Execution reached non opcode");
			return;
		}
		ctx.current_sym = current_sym;

		buxn_chess_copy_stack(&saved_wst, &ctx.entry->state.wst);
		buxn_chess_copy_stack(&saved_rst, &ctx.entry->state.rst);
		if (buxn_chess_op_flag_k(&ctx)) {
			// Apply pop to shadow stack
			buxn_chess_copy_stack(&shadow_wst, &ctx.entry->state.wst);
			buxn_chess_copy_stack(&shadow_rst, &ctx.entry->state.rst);
			ctx.wsp = &shadow_wst;
			ctx.rsp = &shadow_rst;
		} else {
			// Pop directly from stack
			ctx.wsp = &ctx.entry->state.wst;
			ctx.rsp = &ctx.entry->state.rst;
		}

		switch (ctx.current_opcode) {
			BUXN_OPCODE_DISPATCH(BUXN_CHESS_DISPATCH)
		}

		void* region = buxn_chess_begin_mem_region(chess->ctx);
		BUXN_CHESS_DEBUG(
			"WST(%d):%s",
			ctx.entry->state.wst.len,
			buxn_chess_format_stack(chess, ctx.entry->state.wst.content, ctx.entry->state.wst.len).chars
		);
		BUXN_CHESS_DEBUG(
			"RST(%d):%s",
			ctx.entry->state.rst.len,
			buxn_chess_format_stack(chess, ctx.entry->state.rst.content, ctx.entry->state.rst.len).chars
		);
		buxn_chess_end_mem_region(chess->ctx, region);
	}

	// Dump stack before error
	if (ctx.error_region.filename != NULL) {
		void* region = buxn_chess_begin_mem_region(chess->ctx);
		buxn_chess_str_t extra_msg = buxn_chess_printf(
			chess,
			"Stack before error:%s .%s",
			buxn_chess_format_stack(chess, saved_wst.content, saved_wst.len).chars,
			buxn_chess_format_stack(chess, saved_rst.content, saved_rst.len).chars
		);
		buxn_chess_report_info(chess->ctx, &(buxn_asm_report_t){
			.region = &ctx.error_region,
			.message = extra_msg.chars,
		});
		buxn_chess_end_mem_region(chess->ctx, region);
	}
}

// }}}

// Parsing {{{

static buxn_chess_value_t
buxn_chess_parse_value(buxn_chess_t* chess, const buxn_asm_sym_t* sym, size_t len) {
	buxn_chess_value_t value = { 0 };
	if (sym->name[len - 1] == '*') {
		value.name = buxn_chess_strcpy(chess, sym->name, len - 1);
		value.semantics |= BUXN_CHESS_SEM_SIZE_SHORT;
	} else {
		value.name = buxn_chess_strcpy(chess, sym->name, len);
	}

	if (
		value.name.len > 0
		&&
		value.name.chars[0] == '['
		&&
		value.name.chars[value.name.len - 1] == ']'
	) {
		value.semantics |= BUXN_CHESS_SEM_ADDRESS;
	}

	char first_char = sym->name[0];
	if ('A' <= first_char && first_char <= 'Z') {
		value.semantics |= BUXN_CHESS_SEM_NOMINAL;
		value.type = value.name;
	}

	return value;
}

static void
buxn_chess_parse_signature2(buxn_chess_t* chess, const buxn_asm_sym_t* sym) {
	if (sym != NULL) {
		size_t len = strlen(sym->name);
		if ((len == 1 && sym->name[0] == '.')) {
			if (chess->sig_parse_state == BUXN_CHESS_PARSE_WST_IN) {
				chess->sig_parse_state = BUXN_CHESS_PARSE_RST_IN;
			} else if (chess->sig_parse_state == BUXN_CHESS_PARSE_WST_OUT) {
				chess->sig_parse_state = BUXN_CHESS_PARSE_RST_OUT;
			} else {
				buxn_chess_report(
					chess->ctx,
					BUXN_ASM_REPORT_WARNING,
					&(buxn_asm_report_t){
						.message = "Unexpected token in signature",
						.region = &sym->region,
					}
				);
				chess->anno_handler = NULL;
			}
		} else if (len == 2 && sym->name[0] == '-' && sym->name[1] == '-') {
			if (
				chess->sig_parse_state == BUXN_CHESS_PARSE_WST_IN
				|| chess->sig_parse_state == BUXN_CHESS_PARSE_RST_IN
			) {
				chess->sig_parse_state = BUXN_CHESS_PARSE_WST_OUT;
				chess->current_signature->type = BUXN_CHESS_SUBROUTINE;
			} else {
				buxn_chess_report(
					chess->ctx,
					BUXN_ASM_REPORT_WARNING,
					&(buxn_asm_report_t){
						.message = "Unexpected token in signature",
						.region = &sym->region,
					}
				);
				chess->anno_handler = NULL;
			}
		} else if (len == 2 && sym->name[0] == '-' && sym->name[1] == '>') {
			if (
				chess->sig_parse_state == BUXN_CHESS_PARSE_WST_IN
				|| chess->sig_parse_state == BUXN_CHESS_PARSE_RST_IN
			) {
				chess->sig_parse_state = BUXN_CHESS_PARSE_WST_OUT;
				chess->current_signature->type = BUXN_CHESS_VECTOR;
			} else {
				buxn_chess_report(
					chess->ctx,
					BUXN_ASM_REPORT_WARNING,
					&(buxn_asm_report_t){
						.message = "Unexpected token in signature",
						.region = &sym->region,
					}
				);
				chess->anno_handler = NULL;
			}
		} else {
			buxn_chess_sig_stack_t* stack;
			switch (chess->sig_parse_state) {
				case BUXN_CHESS_PARSE_WST_IN:
					stack = &chess->current_signature->wst_in;
					break;
				case BUXN_CHESS_PARSE_RST_IN:
					stack = &chess->current_signature->rst_in;
					break;
				case BUXN_CHESS_PARSE_WST_OUT:
					stack = &chess->current_signature->wst_out;
					break;
				case BUXN_CHESS_PARSE_RST_OUT:
					stack = &chess->current_signature->rst_out;
					break;
			}

			if (stack->len < BUXN_CHESS_MAX_ARGS) {
				stack->content[stack->len++] = buxn_chess_parse_value(chess, sym, len);
			} else {
				buxn_chess_report(
					chess->ctx,
					BUXN_ASM_REPORT_WARNING,
					&(buxn_asm_report_t){
						.message = "Too many arguments",
						.region = &sym->region,
					}
				);
				chess->anno_handler = NULL;
			}
		}
	} else {
		buxn_chess_addr_info_t* addr_info = buxn_chess_addr_info(chess, chess->current_label_addr);
		if ((addr_info->value.semantics & BUXN_CHESS_SEM_ROUTINE) == 0) {
			addr_info->value.semantics |= BUXN_CHESS_SEM_ROUTINE;
			addr_info->value.signature = chess->current_signature;
			chess->current_signature = NULL;

			// Queue the vector for verification
			if (addr_info->value.signature->type == BUXN_CHESS_VECTOR) {
				buxn_chess_queue_routine(chess, addr_info);
			}
		} else {
			buxn_chess_report(
				chess->ctx,
				BUXN_ASM_REPORT_WARNING,
				&(buxn_asm_report_t){
					.message = "Routine already has a signature",
					// TODO: report in redundant annotation region
					.region = &chess->current_label.region,
				}
			);
		}

		chess->current_label.name = NULL;
	}
}

static void
buxn_chess_parse_signature(buxn_chess_t* chess, const buxn_asm_sym_t* sym) {
	if (sym != NULL) {
		size_t len = strlen(sym->name);
		if (
			(len == 1 && sym->name[0] == '.')
			|| (len == 2 && sym->name[0] == '-' && sym->name[1] == '-')
			|| (len == 2 && sym->name[0] == '-' && sym->name[1] == '>')
		) {
			// Use the actual parser now that we have confirmed that this is
			// a signature
			if (chess->current_signature == NULL) {
				chess->current_signature = buxn_chess_alloc(
					chess->ctx, sizeof(buxn_chess_signature_t), _Alignof(buxn_chess_signature_t)
				);
			}
			memset(chess->current_signature, 0, sizeof(*chess->current_signature));
			chess->sig_parse_state = BUXN_CHESS_PARSE_WST_IN;

			chess->anno_handler = buxn_chess_parse_signature2;
			for (uint8_t i = 0; i < chess->num_sig_tokens; ++i) {
				if (chess->anno_handler != NULL) {
					chess->anno_handler(chess, &chess->signature_tokens[i]);
				} else {
					break;
				}
			}

			if (chess->anno_handler != NULL) {
				chess->anno_handler(chess, sym);
			}
		} else if (chess->num_sig_tokens < BUXN_CHESS_MAX_SIG_TOKENS) {
			// Buffer the token
			const char* name_copy = buxn_chess_tmp_strcpy(chess, sym->name);
			if (name_copy == NULL) {
				chess->anno_handler = NULL;
				return;
			}

			chess->signature_tokens[chess->num_sig_tokens++] = (buxn_asm_sym_t){
				.name = name_copy,
				.region = sym->region,
			};
		} else {
			chess->anno_handler = NULL;
		}
	}
}

// }}}

buxn_chess_t*
buxn_chess_begin(buxn_asm_ctx_t* ctx) {
	buxn_chess_t* chess = buxn_chess_alloc(ctx, sizeof(buxn_chess_t), _Alignof(buxn_chess_t));
	*chess = (buxn_chess_t){
		.ctx = ctx,
		.success = true,
	};
	return chess;
}

bool
buxn_chess_end(buxn_chess_t* chess) {
	// Automatically enqueue 0x0100 as "RESET" if not already enqueued
	buxn_chess_addr_info_t* reset = buxn_chess_ensure_addr_info(chess, 0x0100);
	if (!reset->queued) {
		reset->value.semantics |= BUXN_CHESS_SEM_ROUTINE;
		reset->value.signature = buxn_chess_alloc(
			chess->ctx, sizeof(buxn_chess_signature_t), _Alignof(buxn_chess_signature_t)
		);
		*reset->value.signature = (buxn_chess_signature_t){
			.type = BUXN_CHESS_VECTOR,
		};
		buxn_chess_queue_routine(chess, reset);
	}
	if (reset->value.name.len == 0) {
		reset->value.name = (buxn_chess_str_t){
			.chars = "RESET",
			.len = BLIT_STRLEN("RESET"),
		};
	}

	// Run everything on the verification list
	while (chess->verification_list != NULL) {
		buxn_chess_entry_t* entry = chess->verification_list;
		chess->verification_list = entry->next;

		buxn_chess_execute(chess, entry);

		buxn_chess_add_entry(&chess->entry_pool, entry);
	}

	return chess->success;
}

void
buxn_chess_handle_symbol(
	buxn_chess_t* chess,
	uint16_t addr,
	const buxn_asm_sym_t* sym
) {
	(void)addr;
	if (sym->type == BUXN_ASM_SYM_COMMENT) {
		if (sym->id == 0) {  // Start
			if (sym->name[1] == '\0' && chess->current_label.name != NULL) {  // Lone '('
				chess->tmp_buff_ptr = chess->tmp_buf;
				chess->num_sig_tokens = 0;
				chess->anno_handler = buxn_chess_parse_signature;
			}
		} else if (sym->id == 1 && sym->name[0] == ')' && sym->name[1] == '\0') {  // End
			if (chess->anno_handler != NULL) {
				chess->anno_handler(chess, NULL);
				chess->anno_handler = NULL;
			}
		} else {  // Intermediate
			if (sym->id == 1) {
				if (chess->anno_handler != NULL) {
					chess->anno_handler(chess, sym);
				}
			} else {
				chess->anno_handler = NULL;
			}
		}
	} else if (sym->type == BUXN_ASM_SYM_LABEL) {
		chess->current_label = *sym;
		chess->current_label_addr = addr;

		buxn_chess_addr_info_t* addr_info = buxn_chess_ensure_addr_info(chess, addr);
		addr_info->value.name = (buxn_chess_str_t){
			.chars = chess->current_label.name,
			.len = strlen(chess->current_label.name),
		};
	} else {
		chess->current_label.name = NULL;
	}

	if (
		sym->type == BUXN_ASM_SYM_OPCODE
		|| sym->type == BUXN_ASM_SYM_LABEL_REF
		|| sym->type == BUXN_ASM_SYM_NUMBER
	) {
		assert(addr >= 256 && "Code is put in zero-page");

		buxn_asm_sym_t* in_sym;
		if (
			chess->last_sym != NULL
			&& sym->type == chess->last_sym->type
			&& sym->region.filename == chess->last_sym->region.filename
			&& sym->region.range.start.byte == chess->last_sym->region.range.start.byte
			&& sym->region.range.end.byte == chess->last_sym->region.range.end.byte
		) {
			in_sym = chess->last_sym;
		} else {
			in_sym = chess->last_sym = buxn_chess_alloc(
				chess->ctx, sizeof(buxn_asm_sym_t), _Alignof(buxn_asm_sym_t)
			);
			*in_sym = *sym;
		}

		chess->symbols[addr] = in_sym;
	}
}
