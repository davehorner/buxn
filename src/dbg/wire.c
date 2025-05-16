#include <buxn/dbg/wire.h>
#include <buxn/dbg/core.h>
#include <buxn/dbg/protocol.h>

static void
buxn_dbg_wire_maybe_reply(buxn_dbg_wire_t* wire) {
	if (wire->out != NULL && wire->should_send_reply) {
		bserial_status_t status = buxn_dbg_protocol_msg(
			wire->out,
			wire->buffer,
			&(buxn_dbg_msg_t){
				.type = BUXN_DBG_MSG_COMMAND_REP,
				.cmd = wire->cmd,
			}
		);
		wire->should_send_reply = false;

		if (status != BSERIAL_OK) {
			wire->out = NULL;
			wire->in = NULL;
		}
	}
}

void
buxn_dbg_begin_exec(buxn_dbg_wire_t* wire, uint16_t addr) {
	if (wire->out != NULL) {
		bserial_status_t status = buxn_dbg_protocol_msg(
			wire->out,
			wire->buffer,
			&(buxn_dbg_msg_t){
				.type = BUXN_DBG_MSG_BEGIN_EXEC,
				.addr = addr,
			}
		);

		if (status != BSERIAL_OK) { wire->out = NULL; }
	}
}

void
buxn_dbg_begin_break(buxn_dbg_wire_t* wire, uint8_t brkp_id) {
	if (wire->out != NULL) {
		bserial_status_t status = buxn_dbg_protocol_msg(
			wire->out,
			wire->buffer,
			&(buxn_dbg_msg_t){
				.type = BUXN_DBG_MSG_BEGIN_BREAK,
				.brkp_id = brkp_id,
			}
		);

		if (status != BSERIAL_OK) { wire->out = NULL; }
	}
}

void
buxn_dbg_next_command(buxn_dbg_wire_t* wire, buxn_dbg_cmd_t* cmd) {
	buxn_dbg_wire_maybe_reply(wire);

	if (wire->in != NULL) {
		buxn_dbg_msg_t msg;
		bserial_status_t status = buxn_dbg_protocol_msg(wire->in, wire->buffer, &msg);
		if (status == BSERIAL_OK && msg.type == BUXN_DBG_MSG_COMMAND_REQ) {
			*cmd = wire->cmd = msg.cmd;
			wire->should_send_reply = true;
		} else {
			wire->in = NULL;
			cmd->type = BUXN_DBG_CMD_RESUME;
		}
	} else {
		cmd->type = BUXN_DBG_CMD_RESUME;
	}
}

void
buxn_dbg_end_break(buxn_dbg_wire_t* wire) {
	buxn_dbg_wire_maybe_reply(wire);

	if (wire->out != NULL) {
		bserial_status_t status = buxn_dbg_protocol_msg(
			wire->out,
			wire->buffer,
			&(buxn_dbg_msg_t){ .type = BUXN_DBG_MSG_END_BREAK }
		);

		if (status != BSERIAL_OK) { wire->out = NULL; }
	}
}

void
buxn_dbg_end_exec(buxn_dbg_wire_t* wire) {
	if (wire->out != NULL) {
		bserial_status_t status = buxn_dbg_protocol_msg(
			wire->out,
			wire->buffer,
			&(buxn_dbg_msg_t){ .type = BUXN_DBG_MSG_END_EXEC }
		);

		if (status != BSERIAL_OK) { wire->out = NULL; }
	}
}
