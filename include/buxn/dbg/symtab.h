#ifndef BUXN_DBG_SYMTAB_H
#define BUXN_DBG_SYMTAB_H

#include "../asm/asm.h"

typedef struct buxn_dbg_symtab_writer_s buxn_dbg_symtab_writer_t;
typedef struct buxn_dbg_symtab_reader_s buxn_dbg_symtab_reader_t;
struct bserial_in_s;
struct bserial_out_s;

typedef enum {
	BUXN_DBG_SYMTAB_OK,
	BUXN_DBG_SYMTAB_IO_ERROR,
	BUXN_DBG_SYMTAB_MALFORMED,
} buxn_dbg_symtab_io_status_t;

typedef enum {
	BUXN_DBG_SYM_OPCODE     = 0,
	BUXN_DBG_SYM_LABEL_REF  = 1,
	BUXN_DBG_SYM_NUMBER     = 2,
	BUXN_DBG_SYM_TEXT       = 3,
	BUXN_DBG_SYM_LABEL      = 4,
} buxn_dbg_sym_type_t;

typedef struct {
	buxn_dbg_sym_type_t type;
	uint16_t id;
	uint16_t addr_min;
	uint16_t addr_max;
	buxn_asm_source_region_t region;
} buxn_dbg_sym_t;

typedef struct buxn_dbg_symtab_s {
	uint32_t num_symbols;
	buxn_dbg_sym_t* symbols;
} buxn_dbg_symtab_t;

typedef struct {
	uint32_t num_files;
	struct bserial_out_s* output;
} buxn_dbg_symtab_writer_opts_t;

typedef struct {
	struct bserial_in_s* input;
} buxn_dbg_symtab_reader_opts_t;

size_t
buxn_dbg_symtab_writer_mem_size(const buxn_dbg_symtab_writer_opts_t* options);

buxn_dbg_symtab_writer_t*
buxn_dbg_make_symtab_writer(void* mem, const buxn_dbg_symtab_writer_opts_t* options);

buxn_dbg_symtab_io_status_t
buxn_dbg_write_symtab(buxn_dbg_symtab_writer_t* writer, const buxn_dbg_symtab_t* symtab);

size_t
buxn_dbg_symtab_reader_mem_size(const buxn_dbg_symtab_reader_opts_t* options);

buxn_dbg_symtab_reader_t*
buxn_dbg_make_symtab_reader(void* mem, const buxn_dbg_symtab_reader_opts_t* options);

buxn_dbg_symtab_io_status_t
buxn_dbg_read_symtab_header(buxn_dbg_symtab_reader_t* reader);

size_t
buxn_dbg_symtab_mem_size(buxn_dbg_symtab_reader_t* reader);

buxn_dbg_symtab_io_status_t
buxn_dbg_read_symtab(buxn_dbg_symtab_reader_t* reader, buxn_dbg_symtab_t* symtab);

#endif
