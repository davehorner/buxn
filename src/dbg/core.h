#ifndef BUXN_DBG_CORE_H
#define BUXN_DBG_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BUXN_DBG_SIZE      1056
#define BUXN_DBG_ALIGNMENT 8

#define BUXN_DBG_BRKP_MEM     (0 << 0)
#define BUXN_DBG_BRKP_DEV     (1 << 0)
#define BUXN_DBG_BRKP_PAUSE   (1 << 1)
#define BUXN_DBG_BRKP_EXEC    (1 << 2)
#define BUXN_DBG_BRKP_LOAD    (1 << 3)
#define BUXN_DBG_BRKP_STORE   (1 << 4)
#define BUXN_DBG_BRKP_TYPE_MASK   0x01

#define BUXN_DBG_BRKP_NONE ((uint8_t)0xff)

typedef struct buxn_dbg_s buxn_dbg_t;
typedef struct buxn_dbg_wire_s buxn_dbg_wire_t;
struct buxn_vm_s;

typedef enum {
	BUXN_DBG_CMD_INFO,
	BUXN_DBG_CMD_RESUME,
	BUXN_DBG_CMD_STEP_OVER,
	BUXN_DBG_CMD_STEP_IN,
	BUXN_DBG_CMD_STEP_OUT,
	BUXN_DBG_CMD_BRKP_GET,
	BUXN_DBG_CMD_BRKP_SET,
	BUXN_DBG_CMD_DEV_READ,
	BUXN_DBG_CMD_DEV_WRITE,
	BUXN_DBG_CMD_MEM_READ,
	BUXN_DBG_CMD_MEM_WRITE,
} buxn_dbg_cmd_type_t;

typedef enum {
	BUXN_DBG_INFO_PC,
	BUXN_DBG_INFO_WST,
	BUXN_DBG_INFO_RST,
	BUXN_DBG_INFO_NBRKPS,
} buxn_dbg_info_type_t;

typedef struct {
	uint16_t addr;
	uint8_t mask;
} buxn_dbg_brkp_t;

typedef struct {
	uint8_t pointer;
	uint8_t data[256];
} buxn_dbg_stack_info_t;

typedef struct buxn_dbg_cmd_s {
	buxn_dbg_cmd_type_t type;

	union {
		struct {
			union {
				uint16_t* pc;
				uint8_t* nbrkps;
				buxn_dbg_stack_info_t* stack;
			};
			buxn_dbg_info_type_t type;
		} info;

		struct {
			buxn_dbg_brkp_t* brkp;
			uint8_t id;
		} brkp_get;

		struct {
			buxn_dbg_brkp_t brkp;
			uint8_t id;
		} brkp_set;

		struct {
			uint8_t* values;
			uint8_t addr;
			uint8_t size;
		} dev_read;

		struct {
			const uint8_t* values;
			uint8_t addr;
			uint8_t size;
		} dev_write;

		struct {
			uint8_t* values;
			uint16_t addr;
			uint16_t size;
		} mem_read;

		struct {
			const uint8_t* values;
			uint16_t addr;
			uint16_t size;
		} mem_write;
	};
} buxn_dbg_cmd_t;

buxn_dbg_t*
buxn_dbg_init(void* mem, buxn_dbg_wire_t* wire);

void
buxn_dbg_request_pause(buxn_dbg_t* dbg);

void
buxn_dbg_hook(buxn_dbg_t* dbg, struct buxn_vm_s* vm, uint16_t pc);

bool
buxn_dbg_should_hook(buxn_dbg_t* dbg);

// Must be provided by the host program

extern void
buxn_dbg_begin_exec(buxn_dbg_wire_t* wire, uint16_t addr);

extern void
buxn_dbg_begin_break(buxn_dbg_wire_t* wire, uint8_t brkp_id);

extern void
buxn_dbg_next_command(buxn_dbg_wire_t* wire, buxn_dbg_cmd_t* cmd);

extern void
buxn_dbg_end_break(buxn_dbg_wire_t* wire);

extern void
buxn_dbg_end_exec(buxn_dbg_wire_t* wire);

#endif
