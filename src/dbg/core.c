#include <string.h>
#include "core.h"
#include "../vm/vm.h"

typedef enum {
	BUXN_DBG_STANDING_CMD_EXECUTE,
	BUXN_DBG_STANDING_CMD_PAUSE,
	BUXN_DBG_STANDING_CMD_STEP_OVER,
	BUXN_DBG_STANDING_CMD_STEP_OUT,
} buxn_dbg_standing_cmd_type_t;

typedef struct {
	buxn_dbg_standing_cmd_type_t type;
	uint8_t rsp;
} buxn_dbg_standing_cmd_t;

typedef enum {
	BUXN_DBG_OP_CLASS_OTHER,
	BUXN_DBG_OP_CLASS_MEM_LOAD,
	BUXN_DBG_OP_CLASS_MEM_STORE,
	BUXN_DBG_OP_CLASS_DEV_LOAD,
	BUXN_DBG_OP_CLASS_DEV_STORE,
} buxn_dbg_opcode_class_t;

struct buxn_dbg_s {
	buxn_dbg_wire_t* wire;
	bool executing;
	buxn_dbg_standing_cmd_t standing_cmd;
	void* vm_userdata;

	uint8_t nbrkps;
	uint8_t brkp_mask;
	buxn_dbg_brkp_t brkps[255];
};

_Static_assert(sizeof(buxn_dbg_t) == BUXN_DBG_SIZE, "Declared size does not match");
_Static_assert(_Alignof(buxn_dbg_t) == BUXN_DBG_ALIGNMENT, "Declared alignment does not match");

buxn_dbg_t*
buxn_dbg_init(void* mem, buxn_dbg_wire_t* wire) {
	buxn_dbg_t* dbg = mem;
	*dbg = (buxn_dbg_t){
		.wire = wire,
	};

	return dbg;
}

void
buxn_dbg_request_pause(buxn_dbg_t* dbg) {
	dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_PAUSE;
}

bool
buxn_dbg_should_hook(buxn_dbg_t* dbg) {
	return dbg->standing_cmd.type != BUXN_DBG_STANDING_CMD_EXECUTE
		&& dbg->nbrkps != 0;
}

void
buxn_dbg_hook(buxn_dbg_t* dbg, struct buxn_vm_s* vm, uint16_t pc) {
	buxn_dbg_wire_t* wire = dbg->wire;

	bool execution_just_started = false;
	if (!dbg->executing) {
		dbg->executing = true;
		execution_just_started = true;
		buxn_dbg_begin_exec(wire, pc);
	}

	bool should_pause = false;
	uint8_t brkp_id = BUXN_DBG_BRKP_NONE;

	// Try to match a breakpoint if there is any
	if (dbg->brkp_mask != 0) {
		buxn_dbg_opcode_class_t opcode_class = BUXN_DBG_OP_CLASS_OTHER;
		uint16_t target_addr1 = 0;
		uint16_t target_addr2 = 0;
		// If there is a memory breakpoint, we'd have to decode the current
		// opcode to see how it targets memory.
		if ((dbg->brkp_mask & (BUXN_DBG_BRKP_LOAD | BUXN_DBG_BRKP_STORE)) > 0) {
			uint8_t opcode = vm->memory[pc];
			uint8_t base_opccode = opcode & 0x1f;
			bool return_stack = (opcode & 0x40) > 0;
			uint8_t* stack = return_stack ? vm->rs : vm->ws;
			uint8_t sp = return_stack ? vm->rsp : vm->wsp;

			if        (base_opccode == 0x16) {  // DEI
				opcode_class = BUXN_DBG_OP_CLASS_DEV_LOAD;
				target_addr1 = stack[(uint8_t)(sp - 1)];
			} else if (base_opccode == 0x17) {  // DEO
				opcode_class = BUXN_DBG_OP_CLASS_DEV_STORE;
				target_addr1 = stack[(uint8_t)(sp - 1)];
			} else if (base_opccode == 0x10) {  // LDZ
				opcode_class = BUXN_DBG_OP_CLASS_MEM_LOAD;
				target_addr1 = stack[(uint8_t)(sp - 1)];
			} else if (base_opccode == 0x11) {  // STZ
				opcode_class = BUXN_DBG_OP_CLASS_MEM_STORE;
				target_addr1 = stack[(uint8_t)(sp - 1)];
			} else if (base_opccode == 0x12) {  // LDR
				opcode_class = BUXN_DBG_OP_CLASS_MEM_LOAD;
				uint8_t stack_top = stack[(uint8_t)(sp - 1)];
				target_addr1 = (uint16_t)((int32_t)pc + 1 + (int32_t)(int8_t)stack_top);
			} else if (base_opccode == 0x13) { // STR
				opcode_class = BUXN_DBG_OP_CLASS_MEM_STORE;
				uint8_t stack_top = stack[(uint8_t)(sp - 1)];
				target_addr1 = (uint16_t)((int32_t)pc + 1 + (int32_t)(int8_t)stack_top);
			} else if (base_opccode == 0x14) { // LDA
				opcode_class = BUXN_DBG_OP_CLASS_MEM_LOAD;
				uint8_t addr_lo = stack[(uint8_t)(sp - 1)];
				uint8_t addr_hi = stack[(uint8_t)(sp - 2)];
				target_addr1 = ((uint16_t)addr_hi << 8) | (uint16_t)addr_lo;
			} else if (base_opccode == 0x15) { // STA
				opcode_class = BUXN_DBG_OP_CLASS_MEM_STORE;
				uint8_t addr_lo = stack[(uint8_t)(sp - 1)];
				uint8_t addr_hi = stack[(uint8_t)(sp - 2)];
				target_addr1 = ((uint16_t)addr_hi << 8) | (uint16_t)addr_lo;
			}

			bool short_mode = (opcode & 0x20) > 0;
			target_addr2 = target_addr1 + (short_mode ? 1 : 0);
		}

		for (uint8_t i = 0; i < dbg->nbrkps; ++i) {
			buxn_dbg_brkp_t brkp = dbg->brkps[i];

			if (brkp.mask == 0) { continue; }

			if ((brkp.mask & BUXN_DBG_BRKP_TYPE_MASK) == BUXN_DBG_BRKP_DEV) {
				// A device execution breakpoint means trapping a vector stored at
				// that address
				if (
					((brkp.mask & BUXN_DBG_BRKP_EXEC) > 0)
					&& execution_just_started
					&& (buxn_vm_dev_load2(vm, (uint8_t)brkp.addr) == pc)
				) {
					brkp_id = i;
					break;
				}

				if (
					(brkp.mask & BUXN_DBG_BRKP_LOAD) > 0
					&& (opcode_class == BUXN_DBG_OP_CLASS_DEV_LOAD)
					&& (target_addr1 == brkp.addr || target_addr2 == brkp.addr)
				) {
					brkp_id = i;
					break;
				}

				if (
					(brkp.mask & BUXN_DBG_BRKP_STORE) > 0
					&& (opcode_class == BUXN_DBG_OP_CLASS_DEV_STORE)
					&& (target_addr1 == brkp.addr || target_addr2 == brkp.addr)
				) {
					brkp_id = i;
					break;
				}
			} else {
				if (
					((brkp.mask & BUXN_DBG_BRKP_EXEC) > 0)
					&& brkp.addr == pc
				) {
					brkp_id = i;
					break;
				}

				if (
					(brkp.mask & BUXN_DBG_BRKP_LOAD) > 0
					&& (opcode_class == BUXN_DBG_OP_CLASS_MEM_LOAD)
					&& (target_addr1 == brkp.addr || target_addr2 == brkp.addr)
				) {
					brkp_id = i;
					break;
				}

				if (
					(brkp.mask & BUXN_DBG_BRKP_STORE) > 0
					&& (opcode_class == BUXN_DBG_OP_CLASS_MEM_STORE)
					&& (target_addr1 == brkp.addr || target_addr2 == brkp.addr)
				) {
					brkp_id = i;
					break;
				}
			}
		}
	}

	// A breakpoint does not always cause a pause if the pause flag is not on
	if (brkp_id != BUXN_DBG_BRKP_NONE) {
		should_pause |= (dbg->brkps[brkp_id].mask & BUXN_DBG_BRKP_PAUSE) > 0;
	}

	// Process standing command
	switch (dbg->standing_cmd.type) {
		case BUXN_DBG_STANDING_CMD_EXECUTE:
			break;
		case BUXN_DBG_STANDING_CMD_PAUSE:
			should_pause = true;
			break;
		case BUXN_DBG_STANDING_CMD_STEP_OUT:
			should_pause |= vm->rsp < dbg->standing_cmd.rsp;
			break;
		case BUXN_DBG_STANDING_CMD_STEP_OVER:
			should_pause |= vm->rsp == dbg->standing_cmd.rsp;
			break;
	}

	if (should_pause || brkp_id != BUXN_DBG_BRKP_NONE) {
		buxn_dbg_begin_break(wire, brkp_id);

		while (should_pause) {
			buxn_dbg_cmd_t cmd;
			buxn_dbg_next_command(wire, &cmd);

			switch (cmd.type) {
				case BUXN_DBG_CMD_INFO:
					switch (cmd.info.type) {
						case BUXN_DBG_INFO_PC:
							*cmd.info.pc = pc;
							break;
						case BUXN_DBG_INFO_WST:
							cmd.info.stack->pointer = vm->wsp;
							memcpy(cmd.info.stack->data, vm->ws, sizeof(vm->ws));
							break;
						case BUXN_DBG_INFO_RST:
							cmd.info.stack->pointer = vm->rsp;
							memcpy(cmd.info.stack->data, vm->rs, sizeof(vm->rs));
							break;
						case BUXN_DBG_INFO_NBRKPS:
							*cmd.info.nbrkps = dbg->nbrkps;
							break;
					}
					break;
				case BUXN_DBG_CMD_RESUME:
					should_pause = false;
					dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_EXECUTE;
					break;
				case BUXN_DBG_CMD_STEP_IN:
					should_pause = false;
					dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_PAUSE;
					break;
				case BUXN_DBG_CMD_STEP_OVER:
					should_pause = false;
					dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_STEP_OVER;
					dbg->standing_cmd.rsp = vm->rsp;
					break;
				case BUXN_DBG_CMD_STEP_OUT:
					should_pause = false;
					if (vm->rsp > 0) {
						dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_STEP_OUT;
						dbg->standing_cmd.rsp = vm->rsp;
					} else {
						dbg->standing_cmd.type = BUXN_DBG_STANDING_CMD_EXECUTE;
					}
					break;
				case BUXN_DBG_CMD_BRKP_GET:
					if (cmd.brkp_get.id != BUXN_DBG_BRKP_NONE) {
						*cmd.brkp_get.brkp = dbg->brkps[cmd.brkp_get.id];
					}
					break;
				case BUXN_DBG_CMD_BRKP_SET:
					if (cmd.brkp_set.id != BUXN_DBG_BRKP_NONE) {
						dbg->brkps[cmd.brkp_set.id] = cmd.brkp_set.brkp;

						uint8_t nbrkps;
						if (cmd.brkp_set.brkp.mask == 0) {
							int i;
							for (i = (int)dbg->nbrkps - 1; i >= 0; --i) {
								if (dbg->brkps[i].mask != 0) {
									break;
								}
							}
							nbrkps = dbg->nbrkps = i + 1;
						} else {
							nbrkps = dbg->nbrkps = cmd.brkp_set.id >= dbg->nbrkps
								? cmd.brkp_set.id + 1
								: dbg->nbrkps;
						}

						uint8_t brkp_mask = 0;
						for (uint8_t i = 0; i < nbrkps; ++i) {
							brkp_mask |= dbg->brkps[i].mask;
						}
						dbg->brkp_mask = brkp_mask;
					}
					break;
				case BUXN_DBG_CMD_DEV_READ:
					for (uint8_t i = 0; i < cmd.dev_read.size; ++i) {
						uint8_t read_addr = cmd.dev_read.addr + i;  // Ensure wrap around
						cmd.dev_read.values[i] = buxn_vm_dei(vm, read_addr);
					}
					break;
				case BUXN_DBG_CMD_DEV_WRITE:
					for (uint8_t i = 0; i < cmd.dev_write.size; ++i) {
						uint8_t write_addr = cmd.dev_write.addr + i;  // Ensure wrap around
						vm->device[write_addr] = cmd.dev_write.values[i];
						buxn_vm_deo(vm, write_addr);
					}
					break;
				case BUXN_DBG_CMD_MEM_READ:
					for (uint16_t i = 0; i < cmd.mem_read.size; ++i) {
						uint16_t read_addr = cmd.mem_read.addr + i;  // Ensure wrap around
						cmd.mem_read.values[i] = vm->memory[read_addr];
					}
					break;
				case BUXN_DBG_CMD_MEM_WRITE:
					for (uint16_t i = 0; i < cmd.mem_write.size; ++i) {
						uint16_t write_addr = cmd.mem_write.addr + i;  // Ensure wrap around
						vm->memory[i] = cmd.mem_write.values[write_addr];
					}
					break;
			}
		}

		buxn_dbg_end_break(wire);
	}

	if (vm->memory[pc] == 0x00) {  // BRK
		buxn_dbg_end_exec(wire);
		dbg->executing = false;
	}
}
