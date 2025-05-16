#ifndef BUXN_DBG_SYMBOL_H
#define BUXN_DBG_SYMBOL_H

#include <bserial.h>
#include <string.h>
#include "../asm/asm.h"

typedef enum {
	BUXN_DBG_SYM_OPCODE     = 0,
	BUXN_DBG_SYM_LABEL_REF  = 1,
	BUXN_DBG_SYM_NUMBER     = 2,
	BUXN_DBG_SYM_TEXT       = 3,
} buxn_dbg_sym_type_t;

typedef struct {
	buxn_dbg_sym_type_t type;
	uint16_t addr_min;
	uint16_t addr_max;
	buxn_asm_source_region_t region;
} buxn_dbg_sym_t;

static inline bserial_ctx_config_t
buxn_dbg_sym_recommended_bserial_config(void) {
	return (bserial_ctx_config_t){
		.max_symbol_len = 48,
		.max_num_symbols = 32,
		.max_record_fields = 16,
		.max_depth = 8,
	};
}

static inline bserial_status_t
buxn_dbg_sym_table(bserial_ctx_t* ctx, uint16_t* count) {
	uint64_t len, limit;
	len = limit = *count;
	BSERIAL_CHECK_STATUS(bserial_table(ctx, &len));

	if (
		(limit > 0 && len > limit)
		|| len > UINT16_MAX
	) {
		return BSERIAL_MALFORMED;
	}
	*count = len;

	return BSERIAL_OK;
}

static inline bserial_status_t
buxn_dbg_sym(bserial_ctx_t* ctx, buxn_dbg_sym_t* entry) {
	BSERIAL_RECORD(ctx, entry) {
		BSERIAL_KEY(ctx, type) {
			uint8_t int_type = entry->type;
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &int_type));
			if (int_type > BUXN_DBG_SYM_TEXT) { return BSERIAL_MALFORMED; }
			entry->type = int_type;
		}

		BSERIAL_KEY(ctx, addr_min) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->addr_min));
		}

		BSERIAL_KEY(ctx, addr_max) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->addr_max));
		}

		BSERIAL_KEY(ctx, filename) {
			uint64_t len;
			if (bserial_mode(ctx) == BSERIAL_MODE_WRITE) {
				len = strlen(entry->region.filename);
			}

			BSERIAL_CHECK_STATUS(bserial_symbol(ctx, &entry->region.filename, &len));
		}

		BSERIAL_KEY(ctx, start.line) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.start.line));
		}

		BSERIAL_KEY(ctx, start.col) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.start.col));
		}

		BSERIAL_KEY(ctx, start.byte) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.start.byte));
		}

		BSERIAL_KEY(ctx, end.line) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.end.line));
		}

		BSERIAL_KEY(ctx, end.col) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.end.col));
		}

		BSERIAL_KEY(ctx, end.byte) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &entry->region.range.end.byte));
		}
	}

	return BSERIAL_OK;
}

#endif
