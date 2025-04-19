// vim: set foldmethod=marker foldlevel=0:
#include "asm.h"
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <chibihash64.h>
#include "opcode_hash.h"
#define BHAMT_HASH_TYPE uint64_t
#include "hamt.h"

#define BUXN_ASM_MAX_TOKEN_LEN 63
#define BUXN_ASM_DEFAULT_LABEL_SCOPE "RESET"
#define BUXN_ASM_RESET_VECTOR 0x0100
#define BUXN_ASM_MAX_PREPROCESSOR_DEPTH 32

typedef struct {
	const char* chars;
	int len;
} buxn_asm_str_t;

typedef struct buxn_asm_pstr_s buxn_asm_pstr_t;

struct buxn_asm_pstr_s {
	buxn_asm_str_t key;
	buxn_asm_pstr_t* children[BHAMT_NUM_CHILDREN];

	BHAMT_HASH_TYPE hash;
	uint16_t export_id;
};

typedef struct {
	buxn_asm_pstr_t* root;
	uint16_t num_exported;
} buxn_asm_strpool_t;

typedef enum {
	BUXN_ASM_UNIT_FILE,
	BUXN_ASM_UNIT_MACRO,
} buxn_asm_unit_type_t;

typedef struct {
	buxn_asm_str_t lexeme;
	const buxn_asm_pstr_t* filename;
	buxn_asm_report_region_t region;
} buxn_asm_token_t;

typedef struct buxn_asm_token_link_s buxn_asm_token_link_t;

struct buxn_asm_token_link_s {
	buxn_asm_token_link_t* next;
	buxn_asm_token_t token;
};

typedef struct {
	buxn_asm_file_t* file;
	const buxn_asm_pstr_t* path;
	buxn_asm_file_pos_t pos;
} buxn_asm_file_unit_t;

typedef struct {
	buxn_asm_token_link_t* first;
	buxn_asm_token_link_t* last;
	buxn_asm_token_link_t* current;
	bool expanding;
} buxn_asm_macro_unit_t;

typedef struct {
	buxn_asm_unit_type_t type;
	union {
		buxn_asm_file_unit_t* file;
		buxn_asm_macro_unit_t* macro;
	};
} buxn_asm_unit_t;

typedef enum {
	BUXN_ASM_SYMTAB_ENTRY_MACRO,
	BUXN_ASM_SYMTAB_ENTRY_LABEL,
} buxn_asm_symtab_entry_type_t;

typedef struct buxn_asm_symtab_node_s buxn_asm_symtab_node_t;

struct buxn_asm_symtab_node_s {
	const buxn_asm_pstr_t* key;
	buxn_asm_symtab_node_t* children[BHAMT_NUM_CHILDREN];
	buxn_asm_symtab_node_t* next;
	buxn_asm_token_t token;
	bool referenced;

	buxn_asm_symtab_entry_type_t type;
	union {
		uint16_t label_address;
		buxn_asm_macro_unit_t macro;
	};
};

typedef struct {
	buxn_asm_symtab_node_t* root;
	buxn_asm_symtab_node_t* first;
	buxn_asm_symtab_node_t* last;
} buxn_asm_symtab_t;

typedef enum {
	BUXN_ASM_LABEL_REF_ZERO,
	BUXN_ASM_LABEL_REF_ABS,
	BUXN_ASM_LABEL_REF_REL,
} buxn_asm_label_ref_type_t;

typedef enum {
	BUXN_ASM_LABEL_REF_BYTE  = 1,
	BUXN_ASM_LABEL_REF_SHORT = 2,
} buxn_asm_label_ref_size_t;

typedef struct buxn_asm_forward_ref_s buxn_asm_forward_ref_t;

struct buxn_asm_forward_ref_s {
	buxn_asm_forward_ref_t* next;
	const buxn_asm_pstr_t* label_name;
	buxn_asm_token_t token;
	uint16_t addr;
	buxn_asm_label_ref_type_t type;
	buxn_asm_label_ref_size_t size;
};

typedef struct {
	void* ctx;
	uint16_t write_addr;

	int preprocessor_depth;
	buxn_asm_macro_unit_t* current_macro;
	char token_buf[BUXN_ASM_MAX_TOKEN_LEN + 1];
	char name_buf[BUXN_ASM_MAX_TOKEN_LEN + 1];

	int read_buf;
	bool has_read_buf;
	bool success;

	buxn_asm_strpool_t strpool;
	buxn_asm_symtab_t symtab;
	buxn_asm_str_t label_scope;
	buxn_asm_forward_ref_t* forward_refs;
	buxn_asm_forward_ref_t* lambdas;
	buxn_asm_forward_ref_t* ref_pool;
} buxn_asm_t;

static bool
buxn_asm_process_file(buxn_asm_t* basm, const buxn_asm_pstr_t* path);

static bool
buxn_asm_process_unit(buxn_asm_t* basm, buxn_asm_unit_t* unit);

// Error/Warning {{{

static bool
buxn_asm_error_ex(buxn_asm_t* basm, const buxn_asm_report_t* report) {
	basm->success = false;
	buxn_asm_report(basm->ctx, BUXN_ASM_REPORT_ERROR, report);
	return false;
}

static bool
buxn_asm_error(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message
) {
	return buxn_asm_error_ex(
		basm,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
		}
	);
}

static bool
buxn_asm_error2(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message,
	const buxn_asm_token_t* related_token
) {
	return buxn_asm_error_ex(
		basm,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
			.related_region = &related_token->region,
		}
	);
}

static void
buxn_asm_warning(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message
) {
	buxn_asm_report(
		basm->ctx,
		BUXN_ASM_REPORT_WARNING,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
		}
	);
}

// }}}

// Tokenize {{{

static int
buxn_asm_peek_char(buxn_asm_t* basm, buxn_asm_file_unit_t* unit) {
	if (!basm->has_read_buf) {
		basm->read_buf = buxn_asm_fgetc(basm->ctx, unit->file);
		basm->has_read_buf = true;
		if (basm->read_buf == BUXN_ASM_IO_ERROR) {
			return buxn_asm_error_ex(
				basm,
				&(buxn_asm_report_t){
					.message = "I/O error",
					.region = &(buxn_asm_report_region_t){
						.filename = unit->path->key.chars
					}
				}
			);
		}
	}

	return basm->read_buf;
}

static void
buxn_asm_consume_char(buxn_asm_t* basm) {
	basm->has_read_buf = false;
}

static int
buxn_asm_get_char(buxn_asm_t* basm, buxn_asm_file_unit_t* unit) {
	int ch = buxn_asm_peek_char(basm, unit);

	buxn_asm_consume_char(basm);
	unit->pos.byte += 1;
	unit->pos.col += 1;

	if (ch == BUXN_ASM_IO_ERROR || ch == BUXN_ASM_IO_EOF) {
		return ch;
	} else if (ch == '\n') {
		unit->pos.line += 1;
		unit->pos.col = 1;
		return ch;
	} else if (ch == '\r') {
		int next_ch = buxn_asm_peek_char(basm, unit);
		if (next_ch == '\n') {
			buxn_asm_consume_char(basm);
		}

		unit->pos.line += 1;
		unit->pos.col = 1;
		return next_ch;
	} else {
		return ch;
	}
}

static bool
buxn_asm_is_runic(char ch) {
	return ch == '{' || ch == '}'
		|| ch == '[' || ch == ']'
		|| ch == '(' || ch == ')'
		|| ch == '|' || ch == '$'
		|| ch == '#'
		|| ch == '"'
		|| ch == '@' || ch == '&' || ch == '/'
		|| ch == ',' || ch == '_'
		|| ch == '.' || ch == '-'
		|| ch == ';' || ch == '='
		|| ch == '!' || ch == '?'
		|| ch == '%' || ch == '~';
}

static bool
buxn_asm_is_uppercased(char ch) {
	return 'A' <= ch && ch <= 'Z';
}

static bool
buxn_asm_is_sep(int ch) {
	return ch == ' '
		|| ch == '\n'
		|| ch == '\t'
		|| ch == '\r'
		|| ch == '\f'
		|| ch == '\v';
}

static bool
buxn_asm_next_token_in_file(
	buxn_asm_t* basm,
	buxn_asm_file_unit_t* unit,
	buxn_asm_token_t* token
) {
	buxn_asm_file_pos_t start = unit->pos;
	int token_len = 0;
	while (true) {
		buxn_asm_file_pos_t end = unit->pos;
		int ch = buxn_asm_get_char(basm, unit);

		if (ch == BUXN_ASM_IO_ERROR) {
			return false;
		} else if (ch == BUXN_ASM_IO_EOF) {
			if (token_len == 0) {
				return false;
			} else {
				basm->token_buf[token_len] = '\0';
				*(token) = (buxn_asm_token_t){
					.filename = unit->path,
					.lexeme = { .chars = basm->token_buf, .len = token_len },
					.region = {
						.filename = unit->path->key.chars,
						.range = { .start = start, .end = end },
					},
				};
				return true;
			}
		} else if (buxn_asm_is_sep(ch)) {
			if (token_len == 0) {
				start = unit->pos;
			} else {
				basm->token_buf[token_len] = '\0';
				*(token) = (buxn_asm_token_t){
					.filename = unit->path,
					.lexeme = { .chars = basm->token_buf, .len = token_len },
					.region = {
						.filename = unit->path->key.chars,
						.range = { .start = start, .end = end },
					},
				};
				return true;
			}
		} else {
			if (token_len < BUXN_ASM_MAX_TOKEN_LEN) {
				basm->token_buf[token_len++] = (char)ch;
			} else {
				return buxn_asm_error_ex(
					basm,
					&(buxn_asm_report_t) {
						.message = "Token is too long",
						.region = &(buxn_asm_report_region_t){
							.filename = unit->path->key.chars,
							.range = { .start = start, .end = end },
						},
					}
				);
			}
		}
	}
}

static bool
buxn_asm_next_token_in_macro(
	buxn_asm_t* basm,
	buxn_asm_macro_unit_t* unit,
	buxn_asm_token_t* token
) {
	(void)basm;
	if (unit->current != NULL) {
		*token = unit->current->token;
		unit->current = unit->current->next;
		return true;
	} else {
		return false;
	}
}

static bool
buxn_asm_next_token(buxn_asm_t* basm, buxn_asm_unit_t* unit, buxn_asm_token_t* token) {
	switch (unit->type) {
		case BUXN_ASM_UNIT_FILE:
			return buxn_asm_next_token_in_file(basm, unit->file, token);
		case BUXN_ASM_UNIT_MACRO:
			return buxn_asm_next_token_in_macro(basm, unit->macro, token);
		default:
			return false;
	}
}

// }}}

// String {{{

static buxn_asm_str_t
buxn_asm_str_pop_front(buxn_asm_str_t str) {
	if (str.len > 0) {
		return (buxn_asm_str_t){ .chars = str.chars + 1, .len = str.len - 1 };
	} else {
		return str;
	}
}

static inline bool
buxn_asm_ptr_eq(const void* lhs, const void* rhs) {
	return lhs == rhs;
}

static inline bool
buxn_asm_str_eq(buxn_asm_str_t lhs, buxn_asm_str_t rhs) {
	return lhs.len == rhs.len
		&& memcmp(lhs.chars, rhs.chars, lhs.len) == 0;
}

static const buxn_asm_pstr_t*
buxn_asm_strintern(buxn_asm_t* basm, buxn_asm_str_t str) {
	BHAMT_HASH_TYPE hash = chibihash64(str.chars, str.len, 0);
	buxn_asm_pstr_t** itr;
	buxn_asm_pstr_t* node;
	BHAMT_SEARCH(basm->strpool.root, itr, node, hash, str, buxn_asm_str_eq);

	if (node != NULL) { return node; }

	node = *itr = buxn_asm_alloc(
		basm->ctx,
		sizeof(buxn_asm_symtab_node_t) + str.len + 1,
		_Alignof(buxn_asm_symtab_node_t)
	);

	char* chars = (char*)node + sizeof(*node);
	memcpy(chars, str.chars, str.len);
	chars[str.len] = '\0';

	*node = (buxn_asm_pstr_t){
		.key = { .chars = chars, .len = str.len },
		.hash = hash,
	};

	return node;
}

static const buxn_asm_pstr_t*
buxn_asm_strfind(buxn_asm_t* basm, buxn_asm_str_t str) {
	BHAMT_HASH_TYPE hash = chibihash64(str.chars, str.len, 0);
	buxn_asm_pstr_t* node;
	BHAMT_GET(basm->strpool.root, node, hash, str, buxn_asm_str_eq);
	return node;
}

static uint16_t
buxn_asm_strexport(buxn_asm_t* basm, const buxn_asm_pstr_t* str) {
	if (str->export_id != 0) { return str->export_id; }


	uint16_t id = ((buxn_asm_pstr_t*)str)->export_id = ++basm->strpool.num_exported;
	buxn_asm_put_string(basm->ctx, id, str->key.chars, str->key.len);
	return str->export_id;
}

static buxn_asm_token_t
buxn_asm_persist_token(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	if (token->lexeme.chars != basm->token_buf) {
		return *token;
	}

	char* str_copy = buxn_asm_alloc(basm->ctx, token->lexeme.len + 1, 1);
	memcpy(str_copy, token->lexeme.chars, token->lexeme.len);
	str_copy[token->lexeme.len] = '\0';
	return (buxn_asm_token_t){
		.filename = token->filename,
		.lexeme = { .chars = str_copy, .len = token->lexeme.len },
		.region = token->region,
	};
}

// }}}

// Symbol {{{

static buxn_asm_symtab_node_t*
buxn_asm_register_symbol(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t name
) {
	if (name.len == 0 || buxn_asm_is_runic(name.chars[0])) {
		buxn_asm_error(basm, token, "Invalid symbol name");
		return NULL;
	}

	const buxn_asm_pstr_t* interned_name = buxn_asm_strintern(basm, name);
	buxn_asm_symtab_node_t** itr;
	buxn_asm_symtab_node_t* node;
	BHAMT_SEARCH(basm->symtab.root, itr, node, interned_name->hash, interned_name, buxn_asm_ptr_eq);

	if (node != NULL) {
		buxn_asm_error2(basm, token, "Duplicated definition", &node->token);
		return NULL;
	}

	node = *itr = buxn_asm_alloc(
		basm->ctx,
		sizeof(buxn_asm_symtab_node_t),
		_Alignof(buxn_asm_symtab_node_t)
	);
	(*node) = (buxn_asm_symtab_node_t){
		.key = interned_name,
		.token = buxn_asm_persist_token(basm, token),
	};

	if (basm->symtab.first == NULL) {
		basm->symtab.first = node;
	}
	if (basm->symtab.last != NULL) {
		basm->symtab.last->next = node;
	}
	basm->symtab.last = node;

	return node;
}

static buxn_asm_symtab_node_t*
buxn_asm_find_symbol(buxn_asm_t* basm, const buxn_asm_pstr_t* name) {
	buxn_asm_symtab_node_t* node;
	BHAMT_GET(basm->symtab.root, node, name->hash, name, buxn_asm_ptr_eq);
	return node;
}

// }}}

// Label {{{

static buxn_asm_symtab_node_t*
buxn_asm_register_label(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t label_name,
	uint16_t write_addr
) {
	buxn_asm_symtab_node_t* symbol = buxn_asm_register_symbol(basm, token, label_name);
	if (symbol == NULL) { return NULL; }

	symbol->type = BUXN_ASM_SYMTAB_ENTRY_LABEL;
	symbol->label_address = write_addr;

	buxn_asm_put_symbol(basm->ctx, write_addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_LABEL,
		.id = buxn_asm_strexport(basm, symbol->key),
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	});

	return symbol;
}

static bool
buxn_asm_resolve_local_name(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t local_name,
	buxn_asm_str_t* name_out
) {
	buxn_asm_str_t scope_name = basm->label_scope;
	if (local_name.len + scope_name.len + 1 > BUXN_ASM_MAX_TOKEN_LEN) {
		return buxn_asm_error(basm, token, "Label name is too long");
	}

	memcpy(basm->name_buf, scope_name.chars, scope_name.len);
	basm->name_buf[scope_name.len] = '/';
	memcpy(basm->name_buf + scope_name.len + 1, local_name.chars, local_name.len);
	basm->name_buf[scope_name.len + local_name.len + 1] = '\0';

	name_out->chars = basm->name_buf;
	name_out->len = scope_name.len + local_name.len + 1;
	return true;
}

static bool
buxn_asm_resolve_label_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t ref,
	buxn_asm_str_t* name_out
) {
	if (ref.len == 0) {
		return buxn_asm_error(basm, token, "Invalid reference");
	} else if (ref.chars[0] == '&' || ref.chars[0] == '/') {
		return buxn_asm_resolve_local_name(basm, token, ref, name_out);
	} else {
		*name_out = ref;
		return true;
	}
}

// }}}

// Codegen {{{

static void
buxn_asm_put_symbol2(void* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	buxn_asm_put_symbol(ctx, addr, sym);
	buxn_asm_put_symbol(ctx, addr + 1, sym);
}

static bool
buxn_asm_emit(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t byte) {
	uint16_t addr = basm->write_addr++;
	if (addr < BUXN_ASM_RESET_VECTOR && byte != 0) {
		return buxn_asm_error(basm, token, "Writing to zero page");
	}

	buxn_asm_put_rom(basm->ctx, addr, byte);
	return true;
}

static bool
buxn_asm_emit2(buxn_asm_t* basm, const buxn_asm_token_t* token, uint16_t short_) {
	if (!buxn_asm_emit(basm, token, short_ >> 8)) { return false; }
	if (!buxn_asm_emit(basm, token, short_ & 0xff)) { return false; }

	return true;
}

static bool
buxn_asm_emit_opcode(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t opcode) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit(basm, token, opcode)) { return false; }

	buxn_asm_put_symbol(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_OPCODE,
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	});

	return true;
}

static bool
buxn_asm_emit_byte(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t byte) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit(basm, token, byte)) { return false; }

	buxn_asm_put_symbol(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_NUMBER,
		.id = byte,
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	});

	return true;
}

static bool
buxn_asm_emit_short(buxn_asm_t* basm, const buxn_asm_token_t* token, uint16_t short_) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit2(basm, token, short_)) { return false; }

	buxn_asm_put_symbol2(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_NUMBER,
		.id = short_,
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	});

	return true;
}

static buxn_asm_forward_ref_t*
buxn_asm_alloc_forward_ref(buxn_asm_t* basm) {
	buxn_asm_forward_ref_t* ref;
	if (basm->ref_pool != NULL) {
		ref = basm->ref_pool;
		basm->ref_pool = ref->next;
	} else {
		ref = buxn_asm_alloc(
			basm->ctx,
			sizeof(buxn_asm_forward_ref_t), _Alignof(buxn_asm_forward_ref_t)
		);
	}

	return ref;
}

static bool
buxn_asm_emit_addr_placeholder(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_size_t size,
	const buxn_asm_pstr_t* label_name
) {
	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_LABEL_REF,
		.id = label_name != NULL ? buxn_asm_strexport(basm, label_name) : 0,
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	};

	uint16_t addr = basm->write_addr;
	switch (size) {
		case BUXN_ASM_LABEL_REF_BYTE:
			if (!buxn_asm_emit(basm, token, 0x01)) { return false; }
			buxn_asm_put_symbol(basm->ctx, addr, &sym);
			break;
		case BUXN_ASM_LABEL_REF_SHORT:
			if (!buxn_asm_emit2(basm, token, 0x01)) { return false; }
			buxn_asm_put_symbol2(basm->ctx, addr, &sym);
			break;
	}

	return true;
}

static bool
buxn_asm_emit_forward_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	const buxn_asm_pstr_t* label_name
) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit_addr_placeholder(basm, token, size, label_name)) {
		return false;
	}

	buxn_asm_forward_ref_t* ref = buxn_asm_alloc_forward_ref(basm);
	*ref = (buxn_asm_forward_ref_t){
		.next = basm->forward_refs,
		.label_name = label_name,
		.token = buxn_asm_persist_token(basm, token),
		.addr = addr,
		.type = type,
		.size = size,
	};
	basm->forward_refs = ref;

	return true;
}

static bool
buxn_asm_emit_lambda_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size
) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit_addr_placeholder(basm, token, size, NULL)) {
		return false;
	}

	buxn_asm_forward_ref_t* ref = buxn_asm_alloc_forward_ref(basm);
	*ref = (buxn_asm_forward_ref_t){
		.next = basm->lambdas,
		.token = *token,
		.addr = addr,
		.type = type,
		.size = size,
	};
	basm->lambdas = ref;

	return true;
}

static uint16_t
buxn_asm_calculate_addr(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	uint16_t from_addr,
	uint16_t to_addr
) {
	switch (type) {
		case BUXN_ASM_LABEL_REF_ZERO:
			if (to_addr > 0xff) {
				buxn_asm_warning(
					basm, token,
					"Taking zero-address of a label past page zero"
				);
			}
			return to_addr & 0xff;
		case BUXN_ASM_LABEL_REF_ABS:
			return to_addr;
		case BUXN_ASM_LABEL_REF_REL:
			return (uint16_t)(int)(from_addr + 2) - (int)to_addr;
		default:
			assert(0 && "Invalid address reference type");
			return 0;
	}
}

static bool
buxn_asm_emit_addr(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_size_t size,
	uint16_t addr,
	const buxn_asm_token_t* token_at_addr,
	const buxn_asm_sym_t* sym
) {
	uint16_t write_addr = basm->write_addr;
	switch (size) {
		case BUXN_ASM_LABEL_REF_BYTE:
			if (size == BUXN_ASM_LABEL_REF_BYTE && addr > UINT8_MAX) {
				return buxn_asm_error2(
					basm, token, "Referenced address is too far", token_at_addr
				);
			}
			if (!buxn_asm_emit(basm, token, addr & 0xff)) { return false; }
			if (sym != NULL) { buxn_asm_put_symbol(basm->ctx, write_addr, sym); }
			break;
		case BUXN_ASM_LABEL_REF_SHORT:
			if (!buxn_asm_emit2(basm, token, addr)) { return false; }
			if (sym != NULL) { buxn_asm_put_symbol2(basm->ctx, write_addr, sym); }
			break;
	}

	return true;
}

static bool
buxn_asm_emit_backward_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	const buxn_asm_symtab_node_t* label,
	bool with_symbol
) {
	assert((label->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) && "Invalid symbol type");

	uint16_t write_addr = basm->write_addr;
	uint16_t addr = buxn_asm_calculate_addr(
		basm, token,
		type,
		write_addr, label->label_address
	);

	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_LABEL_REF,
		.id = buxn_asm_strexport(basm, label->key),
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	};

	return buxn_asm_emit_addr(
		basm, token,
		size,
		addr,
		&label->token,
		with_symbol ? &sym : NULL
	);
}

static bool
buxn_asm_emit_label_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	buxn_asm_str_t label_name
) {
	if (label_name.len == 1 && label_name.chars[0] == '{') {
		return buxn_asm_emit_lambda_ref(basm, token, type, size);
	}

	buxn_asm_str_t full_name;
	if (!buxn_asm_resolve_label_ref(basm, token, label_name, &full_name)) {
		return false;
	}

	if (full_name.len == 0 || buxn_asm_is_runic(full_name.chars[0])) {
		return buxn_asm_error(basm, token, "Invalid reference");
	}

	const buxn_asm_pstr_t* interned_name = buxn_asm_strintern(basm, full_name);
	buxn_asm_symtab_node_t* symbol = buxn_asm_find_symbol(basm, interned_name);
	if (symbol == NULL) {
		return buxn_asm_emit_forward_ref(basm, token, type, size, interned_name);
	} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) {
		return buxn_asm_emit_backward_ref(basm, token, type, size, symbol, true);
	} else {
		return buxn_asm_error(basm, token, "Invalid reference");
	}
}

static bool
buxn_asm_emit_jsi(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	if (!buxn_asm_emit_opcode(basm, token, 0x60)) { return false; }  // JSI
	return buxn_asm_emit_label_ref(
		basm, token,
		BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
		token->lexeme
	);
}

static bool
buxn_asm_resolve(buxn_asm_t* basm) {
	if (basm->lambdas != NULL) {
		buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t){
				.message = "Unbalanced lambda",
				.region = &basm->lambdas->token.region,
			}
		);
	}

	for (
		buxn_asm_forward_ref_t* itr = basm->forward_refs;
		itr != NULL;
		itr = itr->next
	) {
		buxn_asm_symtab_node_t* symbol = buxn_asm_find_symbol(basm, itr->label_name);
		if (symbol == NULL || symbol->type != BUXN_ASM_SYMTAB_ENTRY_LABEL) {
			buxn_asm_error(basm, &itr->token, "Invalid reference");
			continue;
		}

		basm->write_addr = itr->addr;
		buxn_asm_emit_backward_ref(
			basm, &itr->token,
			itr->type, itr->size,
			symbol,
			false
		);
		symbol->referenced = true;
	}

	for (
		buxn_asm_symtab_node_t* itr = basm->symtab.first;
		itr != NULL;
		itr = itr->next
	) {
		if (
			!itr->referenced
			&& !(
				itr->key->key.len > 0
				&& buxn_asm_is_uppercased(itr->key->key.chars[0])
			)
		) {
			buxn_asm_warning(basm->ctx, &itr->token, "Unreferenced symbol");
		}
	}

	return basm->success;
}

// }}}

static bool
buxn_asm_process_comment(
	buxn_asm_t* basm,
	const buxn_asm_token_t* start,
	buxn_asm_unit_t* unit
) {
	int depth = 1;
	buxn_asm_token_t token;
	while (depth > 0 && buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		switch (token.lexeme.chars[0]) {
			case '(': if (token.lexeme.len == 1) { ++depth; } break;
			case ')': if (token.lexeme.len == 1) { --depth; } break;
		}
	}

	if (depth != 0) {
		return buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t){
				.message = "Unbalanced comment",
				.region = &start->region,
			}
		);
	}

	return true;
}

static bool
buxn_asm_process_macro(
	buxn_asm_t* basm,
	const buxn_asm_token_t* start,
	buxn_asm_unit_t* unit
) {
	buxn_asm_str_t macro_name = buxn_asm_str_pop_front(start->lexeme);
	buxn_asm_symtab_node_t* symbol = buxn_asm_register_symbol(basm, start, macro_name);
	if (symbol == NULL) { return false; }

	buxn_asm_token_t token;
	if (
		!buxn_asm_next_token(basm, unit, &token)
		|| (token.lexeme.len != 1 && token.lexeme.chars[0] != '{')
	) {
		return buxn_asm_error(basm, &token, "Macro must be followed by '{'");
	}

	symbol->type = BUXN_ASM_SYMTAB_ENTRY_MACRO;
	buxn_asm_macro_unit_t* macro = &symbol->macro;
	*macro = (buxn_asm_macro_unit_t) { 0 };

	int depth = 1;
	while (depth > 0 && buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		switch (token.lexeme.chars[0]) {
			case '%':
				return buxn_asm_error2(
					basm,
					&token,
					"A macro cannot be defined inside another macro",
					start
				);
			case '{':
				if (token.lexeme.len == 1) { ++depth; }
				break;
			case '}':
				if (token.lexeme.len == 1) { --depth; }
				break;
			default: {
				buxn_asm_token_link_t* token_link = buxn_asm_alloc(
					basm->ctx,
					sizeof(buxn_asm_token_link_t),
					_Alignof(buxn_asm_token_link_t)
				);
				*token_link = (buxn_asm_token_link_t){
					.token = buxn_asm_persist_token(basm, &token),
				};

				if (macro->first == NULL) {
					macro->first = token_link;
				}

				if (macro->last != NULL) {
					macro->last->next = token_link;
				}
				macro->last = token_link;
			} break;
		}
	}

	if (depth != 0) {
		return buxn_asm_error(basm, start, "Macro has unbalanced `{`");
	}

	buxn_asm_put_symbol(basm->ctx, 0, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_MACRO,
		.id = buxn_asm_strexport(basm, symbol->key),
		.region = {
			.source_id = buxn_asm_strexport(basm, start->filename),
			.range = start->region.range,
		},
	});

	return true;
}

static bool
buxn_asm_process_global_label(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	buxn_asm_str_t label_name = buxn_asm_str_pop_front(start->lexeme);
	buxn_asm_symtab_node_t* label = buxn_asm_register_label(basm, start, label_name, basm->write_addr);
	if (label == NULL) { return false; }

	buxn_asm_str_t interned_name = label->key->key;
	int slash_pos;
	for (slash_pos = 0; slash_pos < interned_name.len; ++slash_pos) {
		if (interned_name.chars[slash_pos] == '/') { break; }
	}

	// Name of the scope must exclude the slash ('/') if any
	basm->label_scope = (buxn_asm_str_t){
		.chars = interned_name.chars,
		.len = slash_pos,
	};
	return true;
}

static bool
buxn_asm_process_local_label(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	buxn_asm_str_t label_name;
	buxn_asm_str_t local_name = buxn_asm_str_pop_front(start->lexeme);
	if (!buxn_asm_resolve_local_name(basm, start, local_name, &label_name)) {
		return false;
	}

	return buxn_asm_register_label(basm, start, label_name, basm->write_addr);
}

static bool
buxn_asm_is_number(buxn_asm_str_t str) {
	if (str.len == 0) { return false; }

	for (int i = 0; i < str.len; ++i) {
		char ch = str.chars[i];
		if (!(
			('0' <= ch && ch <= '9')
			|| ('a' <= ch && ch <= 'f')
		)) {
			return false;
		}
	}

	return true;
}

static bool
buxn_asm_parse_number(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t str,
	uint16_t* number_out
) {
	uint16_t number = 0;
	if (str.len == 0 || str.len > 4) {
		return buxn_asm_error(basm, token, "Invalid number");
	}

	for (int i = 0; i < str.len; ++i) {
		char ch = str.chars[str.len - i - 1];
		if ('0' <= ch && ch <= '9') {
			number |= (ch - '0') << (i * 4);
		} else if ('a' <= ch && ch <= 'f') {
			number |= (ch - 'a' + 10) << (i * 4);
		} else {
			return buxn_asm_error(basm, token, "Invalid number");
		}
	}

	*number_out = number;
	return true;
}

static bool
buxn_asm_resolve_padding(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	uint16_t* padding_out
) {
	buxn_asm_str_t padding_str = buxn_asm_str_pop_front(token->lexeme);
	if (padding_str.len == 0) {
		return buxn_asm_error(basm, token, "Invalid padding");
	} else if (buxn_asm_is_number(padding_str)) {
		return buxn_asm_parse_number(basm, token, padding_str, padding_out);
	} else {
		buxn_asm_str_t padding_label;
		if (!buxn_asm_resolve_label_ref(basm, token, padding_str, &padding_label)) {
			return false;
		}

		const buxn_asm_pstr_t* interned_name = buxn_asm_strfind(basm, padding_label);
		if (interned_name == NULL) {
			return buxn_asm_error(basm, token, "Undeclared label is used for padding");
		}

		buxn_asm_symtab_node_t* symbol = buxn_asm_find_symbol(basm, interned_name);
		if (symbol == NULL) {
			return buxn_asm_error(basm, token, "Undeclared label is used for padding");
		}
		if (symbol->type != BUXN_ASM_SYMTAB_ENTRY_LABEL) {
			return buxn_asm_error(basm, token, "Invalid symbol is being used for padding");
		}

		*padding_out = symbol->label_address;
		return true;
	}
}

static bool
buxn_asm_process_abs_padding(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t padding;
	if (!buxn_asm_resolve_padding(basm, start, &padding)) {
		return false;
	}

	basm->write_addr = padding;
	return true;
}

static bool
buxn_asm_process_rel_padding(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t padding;
	if (!buxn_asm_resolve_padding(basm, start, &padding)) {
		return false;
	}

	uint16_t old_addr = basm->write_addr;
	uint16_t new_addr = basm->write_addr += padding;
	if (new_addr < old_addr) {
		buxn_asm_warning(basm->ctx, start, "Relative padding caused wrap around");
	}

	return true;
}

static bool
buxn_asm_process_lit_number(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t number;
	buxn_asm_str_t str = buxn_asm_str_pop_front(start->lexeme);
	if (!buxn_asm_parse_number(basm, start, str, &number)) {
		return false;
	}

	if (str.len <= 2) {
		if (!buxn_asm_emit_opcode(basm, start, 0x80)) { return false; }  // LIT
		if (!buxn_asm_emit_byte(basm, start, number)) { return false; }
	} else {
		if (!buxn_asm_emit_opcode(basm, start, 0xa0)) { return false; }  // LIT2
		if (!buxn_asm_emit_short(basm, start, number)) { return false; }
	}

	return true;
}

static bool
buxn_asm_process_raw_number(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	uint16_t number;
	if (!buxn_asm_parse_number(basm, token, token->lexeme, &number)) {
		return false;
	}

	if (token->lexeme.len <= 2) {
		if (!buxn_asm_emit_byte(basm, token, number)) { return false; }
	} else {
		if (!buxn_asm_emit_short(basm, token, number)) { return false; }
	}

	return true;
}

static bool
buxn_asm_expand_macro(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_macro_unit_t* macro
) {
	if (basm->preprocessor_depth >= BUXN_ASM_MAX_PREPROCESSOR_DEPTH) {
		return buxn_asm_error(basm, token, "Max preprocessor depth depth reached");
	}

	if (macro->expanding) {
		return buxn_asm_error(basm, token, "Macro recursion detected");
	}

	macro->expanding = true;
	macro->current = macro->first;
	++basm->preprocessor_depth;
	bool success = buxn_asm_process_unit(basm, &(buxn_asm_unit_t){
		.type = BUXN_ASM_UNIT_MACRO,
		.macro = macro,
	});
	--basm->preprocessor_depth;
	macro->current = NULL;
	macro->expanding = false;

	// Append an error to explain expansion chain
	if (!success) {
		buxn_asm_error(basm, token, "Error while expanding macro");
	}

	return success;
}

static bool
buxn_asm_process_lambda_close(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	buxn_asm_forward_ref_t* ref = basm->lambdas;
	if (ref == NULL) {
		return buxn_asm_error(basm, token, "Unbalanced lambda");
	}

	uint16_t current_addr = basm->write_addr;
	uint16_t addr = buxn_asm_calculate_addr(
		basm, &ref->token,
		ref->type,
		ref->addr, current_addr
	);

	if (!buxn_asm_emit_addr(basm, &ref->token, ref->size, addr, token, NULL)) {
		return false;
	}

	basm->lambdas = ref->next;
	ref->next = basm->ref_pool;
	basm->ref_pool = ref;

	return true;
}

static bool
buxn_asm_process_word(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	assert((!buxn_asm_is_runic(token->lexeme.chars[0])) && "Runic word encountered");
	// Inline buxn_asm_strfind here so we get the hash
	BHAMT_HASH_TYPE initial_hash = chibihash64(token->lexeme.chars, token->lexeme.len, 0);
	const buxn_asm_pstr_t* interned_name;
	BHAMT_GET(basm->strpool.root, interned_name, initial_hash, token->lexeme, buxn_asm_str_eq);
	if (interned_name == NULL) {
		// Check whether it's an opcode using the pre-calculated
		// minimal perfect hash
		int seed_slot = (int)(initial_hash & 0xff);
		int seed = buxn_opcode_hash_seeds[seed_slot];
		int final_slot;
		if (seed < 0) {
			final_slot = -seed - 1;
		} else {
			uint64_t final_hash = chibihash64(token->lexeme.chars, token->lexeme.len, seed);
			final_slot = (int)(final_hash & 0xff);
		}

		if (strcmp(token->lexeme.chars, buxn_hashed_opcode_names[final_slot]) == 0) {
			return buxn_asm_emit_opcode(basm, token, buxn_hashed_opcode_values[final_slot]);
		} else {
			return buxn_asm_emit_jsi(basm, token);
		}
	} else {
		buxn_asm_symtab_node_t* symbol = buxn_asm_find_symbol(basm, interned_name);
		if (symbol == NULL) {
			if (!buxn_asm_emit_opcode(basm, token, 0x60)) { return false; }  // JSI
			return buxn_asm_emit_forward_ref(
				basm, token,
				BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
				interned_name
			);
		} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_MACRO) {
			symbol->referenced = true;
			return buxn_asm_expand_macro(basm, token, &symbol->macro);
		} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) {
			symbol->referenced = true;
			if (!buxn_asm_emit_opcode(basm, token, 0x60)) { return false; }  // JSI
			return buxn_asm_emit_backward_ref(
				basm, token,
				BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
				symbol,
				true
			);
		} else {
			return buxn_asm_error(basm, token, "Unknown symbol type");
		}
	}
}

static bool
buxn_asm_process_text(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_TEXT,
		.region = {
			.source_id = buxn_asm_strexport(basm, token->filename),
			.range = token->region.range,
		},
	};
	uint16_t addr = basm->write_addr;
	for (int i = 1; i < token->lexeme.len; ++i) {
		if (!buxn_asm_emit(basm, token, token->lexeme.chars[i])) {
			return false;
		}
		buxn_asm_put_symbol(basm->ctx, addr + i - 1, &sym);
	}

	return true;
}

static bool
buxn_asm_process_unit(buxn_asm_t* basm, buxn_asm_unit_t* unit) {
	buxn_asm_token_t token;
	while (buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		switch (token.lexeme.chars[0]) {
			case '(':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}

				if (!buxn_asm_process_comment(basm, &token, unit)) {
					return false;
				}

				break;
			case ')':
				return buxn_asm_error(basm, &token, "Unexpected rune");
			case '[':
			case ']':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				break;
			case '~': {
				if (basm->preprocessor_depth >= BUXN_ASM_MAX_PREPROCESSOR_DEPTH) {
					return buxn_asm_error(basm, &token, "Max preprocessor depth depth reached");
				}

				const buxn_asm_pstr_t* included_filename = buxn_asm_strintern(
					basm, buxn_asm_str_pop_front(token.lexeme)
				);
				++basm->preprocessor_depth;
				bool success = buxn_asm_process_file(basm, included_filename);
				--basm->preprocessor_depth;

				if (!success) {
					// Append another error to explain include chain
					return buxn_asm_error(
						basm,
						&(buxn_asm_token_t){
							.filename = token.filename,
							.lexeme = included_filename->key,
							.region = token.region,
						},
						"Error while processing include"
					);
				}
			} break;
			case '%':
				if (!buxn_asm_process_macro(basm, &token, unit)) {
					return false;
				}
				break;
			case '@':
				if (!buxn_asm_process_global_label(basm, &token)) {
					return false;
				}
				break;
			case '&':
				if (!buxn_asm_process_local_label(basm, &token)) {
					return false;
				}
				break;
			case '!':
				if (!buxn_asm_emit_opcode(basm, &token, 0x40)) {  // JMI
					return false;
				}
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme)
				)) {
					return false;
				}
				break;
			case '?':
				if (!buxn_asm_emit_opcode(basm, &token, 0x20)) {  // JCI
					return false;
				}
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme)
				)) {
					return false;
				}
				break;
			case '}':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				if (!buxn_asm_process_lambda_close(basm, &token)) {
					return false;
				}
				break;
			case '{':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				// fall-through
			case '/':
				if (!buxn_asm_emit_jsi(basm, &token)) {
					return false;
				}
				break;
			case '|':
				if (!buxn_asm_process_abs_padding(basm, &token)) {
					return false;
				}
				break;
			case '$':
				if (!buxn_asm_process_rel_padding(basm, &token)) {
					return false;
				}
				break;
			case '#':
				if (!buxn_asm_process_lit_number(basm, &token)) {
					return false;
				}
				break;
			case '.':
				if (!buxn_asm_emit_opcode(basm, &token, 0x80)) {  // LIT
					return false;
				}
				// fallthrough
			case '-':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_ZERO,
					BUXN_ASM_LABEL_REF_BYTE,
					buxn_asm_str_pop_front(token.lexeme)
				)) {
					return false;
				}
				break;
			case ',':
				if (!buxn_asm_emit_opcode(basm, &token, 0x80)) {  // LIT
					return false;
				}
				// fallthrough
			case '_':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_BYTE,
					buxn_asm_str_pop_front(token.lexeme)
				)) {
					return false;
				}
				break;
			case ';':
				if (!buxn_asm_emit_opcode(basm, &token, 0xa0)) {  // LIT2
					return false;
				}
				// fallthrough
			case '=':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_ABS,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme)
				)) {
					return false;
				}
				break;
			case '"':
				if (!buxn_asm_process_text(basm, &token)) {
					return false;
				}
				break;
			default: {
				if (buxn_asm_is_number(token.lexeme)) {
					if (!buxn_asm_process_raw_number(basm, &token)) {
						return false;
					}
				} else {
					if (!buxn_asm_process_word(basm, &token)) {
						return false;
					}
				}
			} break;
		}
	}

	return basm->success;
}

static bool
buxn_asm_process_file(buxn_asm_t* basm, const buxn_asm_pstr_t* path) {
	buxn_asm_file_t* file = buxn_asm_fopen(basm->ctx, path->key.chars);
	if (file == NULL) {
		return buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t) {
				.message = "Could not open file",
				.region = &(buxn_asm_report_region_t){ .filename = path->key.chars }
			}
		);
	}

	buxn_asm_unit_t unit = {
		.type = BUXN_ASM_UNIT_FILE,
		.file = &(buxn_asm_file_unit_t){
			.file = file,
			.path = path,
			.pos = {
				.line = 1,
				.col = 1,
				.byte = 0,
			},
		}
	};
	bool success = buxn_asm_process_unit(basm, &unit);

	buxn_asm_fclose(basm->ctx, file);
	return success;
}

bool
buxn_asm(buxn_asm_ctx_t* ctx, const char* filename) {
	buxn_asm_t basm = {
		.ctx = ctx,
		.write_addr = BUXN_ASM_RESET_VECTOR,
		.success = true,
		.label_scope = {
			.chars = BUXN_ASM_DEFAULT_LABEL_SCOPE,
			.len = sizeof(BUXN_ASM_DEFAULT_LABEL_SCOPE) - 1,
		},
	};

	const buxn_asm_pstr_t* interned_name = buxn_asm_strintern(
		&basm,
		(buxn_asm_str_t){.chars = filename, .len = strlen(filename) }
	);
	if (!buxn_asm_process_file(&basm, interned_name)) {
		return basm.success;
	}

	if (!buxn_asm_resolve(&basm)) {
		return basm.success;
	}

	return basm.success;
}
