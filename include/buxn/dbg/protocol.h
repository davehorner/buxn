#ifndef BUXN_DBG_PROTOCOL_H
#define BUXN_DBG_PROTOCOL_H

#include <bserial.h>
#include "core.h"

#define BUXN_DBG_MAX_MEM_ACCESS_SIZE 4096

typedef enum {
	BUXN_DBG_MSG_BEGIN_EXEC   = 0,
	BUXN_DBG_MSG_END_EXEC     = 1,
	BUXN_DBG_MSG_BEGIN_BREAK  = 2,
	BUXN_DBG_MSG_END_BREAK    = 3,
	BUXN_DBG_MSG_PAUSED       = 4,
	BUXN_DBG_MSG_COMMAND_REQ  = 5,
	BUXN_DBG_MSG_COMMAND_REP  = 6,
} buxn_dbg_msg_type_t;

typedef struct {
	buxn_dbg_msg_type_t type;
	union {
		uint16_t addr;
		uint8_t brkp_id;
		buxn_dbg_cmd_t cmd;
	};
} buxn_dbg_msg_t;

typedef uint8_t buxn_dbg_msg_buffer_t[BUXN_DBG_MAX_MEM_ACCESS_SIZE];

static inline bserial_ctx_config_t
buxn_dbg_protocol_recommended_bserial_config(void) {
	// TODO: the numbers can be lower
	return (bserial_ctx_config_t){
		.max_symbol_len = 48,
		.max_num_symbols = 32,
		.max_record_fields = 8,
		.max_depth = 4,
	};
}

bserial_status_t
buxn_dbg_protocol_msg_header(bserial_ctx_t* ctx, buxn_dbg_msg_t* msg);

bserial_status_t
buxn_dbg_protocol_msg_body(
	bserial_ctx_t* ctx,
	buxn_dbg_msg_buffer_t buffer,
	buxn_dbg_msg_t* msg
);

static inline bserial_status_t
buxn_dbg_protocol_msg(
	bserial_ctx_t* ctx,
	buxn_dbg_msg_buffer_t buffer,
	buxn_dbg_msg_t* msg
) {
	BSERIAL_CHECK_STATUS(buxn_dbg_protocol_msg_header(ctx, msg));
	BSERIAL_CHECK_STATUS(buxn_dbg_protocol_msg_body(ctx, buffer, msg));
	return BSERIAL_OK;
}

#endif
