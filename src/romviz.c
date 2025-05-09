#include <termbox2.h>
#include <barena.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <bhash.h>
#include "vm/opcodes.h"
#define BSERIAL_STDIO
#include "dbg/symbol.h"

#define SPACE_PER_BYTE 3
#define HEADER_LINES 3

#define DEFINE_OPCODE_NAME(NAME, VALUE) \
	[VALUE] = STRINGIFY(NAME),

#define STRINGIFY(X) STRINGIFY2(X)
#define STRINGIFY2(X) #X

typedef struct {
	char* content;
	int size;
} source_t;

typedef BHASH_TABLE(const char*, source_t) source_set_t;

static const char* opcode_names[256] = {
	BUXN_OPCODE_DISPATCH(DEFINE_OPCODE_NAME)
};

static buxn_dbg_sym_t*
find_symbol(
	uint16_t address,
	int* current_index,
	buxn_dbg_sym_t* symbols,
	int num_symbols
) {
	int index = *current_index;
	for (; index < num_symbols; ++index) {
		if (symbols[index].addr_min <= address && address <= symbols[index].addr_max) {
			*current_index = index;
			return &symbols[index];
		}

		if (symbols[index].addr_min > address) {
			*current_index = index;
			return NULL;
		}
	}

	*current_index = index;
	return NULL;
}

int
main(int argc, const char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: buxn-romviz file.rom\n");
		return 1;
	}

	int exit_code = 1;
	barena_pool_t pool;
	barena_pool_init(&pool, 1);
	barena_t arena;
	barena_init(&arena, &pool);
	source_set_t sources;
	// Since all strings are interned, we can just treat them like values
	bhash_init(&sources, bhash_config_default());

	uint8_t* rom = NULL;
	int rom_size = 0;
	{
		FILE* rom_file = fopen(argv[1], "rb");
		if (rom_file == NULL) {
			fprintf(stderr, "Could not open rom file: %s\n", strerror(errno));
			goto end;
		}

		fseek(rom_file, 0, SEEK_END);
		long size = ftell(rom_file);
		if (size < 0) {
			fclose(rom_file);
			fprintf(stderr, "Could not read rom file: %s\n", strerror(errno));
			goto end;
		}
		rom = barena_malloc(&arena, size);
		rom_size = (uint16_t)size;

		fseek(rom_file, 0, SEEK_SET);
		if (fread(rom, size, 1, rom_file) != 1) {
			fclose(rom_file);
			fprintf(stderr, "Could not read rom file: %s\n", strerror(errno));
			goto end;
		}

		fclose(rom_file);
	}

	buxn_dbg_sym_t* debug_symbols = NULL;
	int num_symbols = 0;
	{
		size_t len = strlen(argv[1]) + 5;
		char* dbg_filename = barena_malloc(&arena, len);
		snprintf(dbg_filename, len, "%s.dbg", argv[1]);
		FILE* dbg_file = fopen(dbg_filename, "rb");
		if (dbg_file == NULL) {
			fprintf(stderr, "Could not open debug file: %s\n", strerror(errno));
			goto end;
		}

		bserial_ctx_config_t config = buxn_dbg_sym_recommended_bserial_config();
		void* mem = barena_malloc(&arena, bserial_ctx_mem_size(config));
		bserial_stdio_in_t stdio_in;
		bserial_in_t* in = bserial_stdio_init_in(&stdio_in, dbg_file);
		bserial_ctx_t* bserial = bserial_make_ctx(mem, config, in, NULL);

		uint16_t count;
		if (buxn_dbg_sym_table(bserial, &count) != BSERIAL_OK) {
			fprintf(stderr, "Error while reading debug file\n");
			fclose(dbg_file);
			goto end;
		}
		num_symbols = count;
		debug_symbols = barena_memalign(
			&arena,
			sizeof(buxn_dbg_sym_t) * num_symbols,
			_Alignof(buxn_dbg_sym_t)
		);

		for (int i = 0; i < num_symbols; ++i) {
			if (buxn_dbg_sym(bserial, &debug_symbols[i]) != BSERIAL_OK) {
				fprintf(stderr, "Error while reading debug file\n");
				fclose(dbg_file);
				goto end;
			}
		}

		fclose(dbg_file);
	}

	int view_pos = 0;
	int line_offset = 0;

    tb_init();

	struct tb_event event;
	bool running = true;
	while (running) {
		tb_clear();

		int width = tb_width();
		int height = tb_height();

		int num_bytes_per_row = (width / SPACE_PER_BYTE) / 4 * 4;

		int focus_line = view_pos / num_bytes_per_row;
		if (focus_line < line_offset) {
			line_offset = focus_line;
		}
		int last_visible_line = line_offset + height - HEADER_LINES - 1;
		if (focus_line > last_visible_line) {
			line_offset += (focus_line - last_visible_line);
		}

		// To save time on symbol search, instead of starting from the beginning,
		// just start searching from the *previous* // line.
		// Some symbols can span a line so backing up by one line ensures we
		// can always find the right symbol.
		int symbol_index = line_offset > 0 ? (line_offset - 1) * num_bytes_per_row : 0;
		buxn_dbg_sym_t* focused_symbol = NULL;
		for (int y = 0; y < height - HEADER_LINES; ++y) {
			for (int x = 0; x < num_bytes_per_row; ++x) {
				int index = x + (y + line_offset) * num_bytes_per_row;
				if (index >= rom_size) { goto end_draw; }

				buxn_dbg_sym_t* symbol = find_symbol(
					index + 256, &symbol_index, debug_symbols, num_symbols
				);
				if (index == view_pos) {
					focused_symbol = symbol;
				}

				uintattr_t background = index == view_pos ? TB_WHITE : TB_DEFAULT;
				uintattr_t foreground = TB_WHITE;
				if (symbol == NULL) {
					foreground = TB_WHITE;
				} else if (symbol->type == BUXN_DBG_SYM_TEXT) {
					foreground = TB_GREEN;
				} else if (symbol->type == BUXN_DBG_SYM_OPCODE) {
					foreground = TB_CYAN;
				} else if (symbol->type == BUXN_DBG_SYM_NUMBER) {
					foreground = TB_RED;
				} else if (symbol->type == BUXN_DBG_SYM_LABEL_REF) {
					foreground = TB_YELLOW;
				}

				tb_printf(x * SPACE_PER_BYTE, y + HEADER_LINES, foreground, background, "%02x", rom[index]);
			}
		}
end_draw:

		if (focused_symbol) {
			// Print source location
			tb_printf(
				0, 0,
				TB_WHITE, TB_DEFAULT,
				"Source: %s (%d:%d:%d - %d:%d:%d)",
				focused_symbol->filename,
				focused_symbol->range.start.line, focused_symbol->range.start.col, focused_symbol->range.start.byte,
				focused_symbol->range.end.line, focused_symbol->range.end.col, focused_symbol->range.end.byte
			);

			// Print type
			const char* type = "Unknown";
			switch (focused_symbol->type) {
				case BUXN_DBG_SYM_OPCODE:
					type = "opcode";
					break;
				case BUXN_DBG_SYM_TEXT:
					type = "text";
					break;
				case BUXN_DBG_SYM_NUMBER:
					type = "number";
					break;
				case BUXN_DBG_SYM_LABEL_REF:
					type = "label";
					break;
			}
			if (focused_symbol->type == BUXN_DBG_SYM_OPCODE) {
				tb_printf(0, 1, TB_WHITE, TB_DEFAULT, "Type: %s (%s)", type, opcode_names[rom[view_pos]]);
			} else {
				tb_printf(0, 1, TB_WHITE, TB_DEFAULT, "Type: %s", type);
			}

			// Print source text
			bhash_index_t index = bhash_find(&sources, focused_symbol->filename);
			source_t source = { 0 };
			if (!bhash_is_valid(index)) {
				FILE* source_file = fopen(focused_symbol->filename, "rb");
				long file_size = 0;
				if (source_file != NULL) {
					fseek(source_file, 0, SEEK_END);
					file_size = ftell(source_file);
				}

				if (file_size > 0) {
					fseek(source_file, 0, SEEK_SET);
					source.content = barena_memalign(&arena, file_size, _Alignof(char));
					if (fread(source.content, file_size, 1, source_file) == 1) {
						source.size = (int)file_size;
					}
				}

				if (source_file != NULL) {
					fclose(source_file);
				}

				bhash_put(&sources, focused_symbol->filename, source);
			} else {
				source = sources.values[index];
			}

			if (source.size > 0) {
				int start = focused_symbol->range.start.byte;
				start = start >= source.size ? source.size - 1 : start;
				int end = focused_symbol->range.end.byte;
				end = end >= source.size ? source.size - 1 : end;

				start = start < 0 ? 0 : start;
				end = end < start ? start : end;
				int count = end - start;

				uintattr_t foreground = TB_WHITE;
				if (focused_symbol->type == BUXN_DBG_SYM_TEXT) {
					foreground = TB_GREEN;
				} else if (focused_symbol->type == BUXN_DBG_SYM_OPCODE) {
					foreground = TB_CYAN;
				} else if (focused_symbol->type == BUXN_DBG_SYM_NUMBER) {
					foreground = TB_RED;
				} else if (focused_symbol->type == BUXN_DBG_SYM_LABEL_REF) {
					foreground = TB_YELLOW;
				}

				size_t out;
				tb_print_ex(0, 2, TB_WHITE, TB_DEFAULT, &out, "Text: ");
				tb_printf(out, 2, foreground, TB_DEFAULT, "%.*s", count, source.content + start);
			}
		}

		tb_present();

		int poll_result = tb_poll_event(&event);
		while (poll_result == TB_OK) {
			switch (event.type) {
				case TB_EVENT_KEY:
					if (
						event.key == TB_KEY_CTRL_C
						|| event.key == TB_KEY_ESC
						|| event.ch == 'q'
					) {
						running = false;
					} else if (
						event.key == TB_KEY_ARROW_RIGHT
						|| event.ch == 'l'
					) {
						view_pos = view_pos + 1 < rom_size ? view_pos + 1 : view_pos;
					} else if (
						event.key == TB_KEY_ARROW_LEFT
						|| event.ch == 'h'
					) {
						view_pos = view_pos - 1 >= 0 ? view_pos - 1 : view_pos;
					} else if (
						event.key == TB_KEY_ARROW_UP
						|| event.ch == 'k'
					) {
						view_pos = view_pos - num_bytes_per_row >= 0 ? view_pos - num_bytes_per_row : 0;
					} else if (
						event.key == TB_KEY_ARROW_DOWN
						|| event.ch == 'j'
					) {
						view_pos = view_pos + num_bytes_per_row < rom_size ? view_pos + num_bytes_per_row : rom_size - 1;
					}
					break;
			}
			poll_result = tb_peek_event(&event, 0);
		}
	}

	exit_code = 0;
end:
	tb_shutdown();

	bhash_cleanup(&sources);
	barena_reset(&arena);
	barena_pool_cleanup(&pool);
	return exit_code;
}

#define TB_IMPL
#include <termbox2.h>

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <bserial.h>
#include <bhash.h>
