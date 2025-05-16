#ifndef BUXN_DBG_WIRE_H
#define BUXN_DBG_WIRE_H

#include <stdint.h>
#include "core.h"
#include "protocol.h"

struct buxn_dbg_wire_s {
	struct bserial_ctx_s* in;
	struct bserial_ctx_s* out;
	bool should_send_reply;
	buxn_dbg_cmd_t cmd;
	buxn_dbg_msg_buffer_t buffer;
};

#endif
