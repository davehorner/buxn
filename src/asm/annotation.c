#include <buxn/asm/annotation.h>
#include <string.h>

void
buxn_anno_handle_symbol(
	buxn_anno_spec_t* spec,
	uint16_t addr,
	const buxn_asm_sym_t* sym
) {
	buxn_asm_sym_type_t sym_type = sym->type;
	if (
		sym_type == BUXN_ASM_SYM_LABEL
		|| sym_type == BUXN_ASM_SYM_LABEL_REF
		|| sym_type == BUXN_ASM_SYM_OPCODE
		|| sym_type == BUXN_ASM_SYM_NUMBER
		|| sym_type == BUXN_ASM_SYM_TEXT
	) {
		spec->last_rom_addr = addr;
		spec->last_rom_sym = *sym;
	}

	if (sym_type == BUXN_ASM_SYM_COMMENT) {
		if (sym->id == 0) {  // Start
			spec->comment_start = *sym;
			spec->comment_first_token.name = NULL;
			spec->comment_last_token.name = NULL;

			spec->current_annotation = NULL;
			if (sym->name[1] == '\0') {  // A bare '('
				spec->comment_kind = spec->current_def.name != NULL
					? BUXN_ANNO_COMMENT_MIGHT_BE_TYPE
					: BUXN_ANNO_COMMENT_IS_TEXT;
			} else {
				spec->comment_kind = BUXN_ANNO_COMMENT_IS_TEXT;
				// Find a matching annotation
				for (size_t i = 0; i < spec->num_annotations; ++i) {
					buxn_anno_t* anno = &spec->annotations[i];
					if (strcmp(anno->name, sym->name + 1) == 0) {
						spec->current_annotation = anno;
						spec->comment_kind = BUXN_ANNO_COMMENT_IS_CUSTOM_ANNOTATION;
						break;
					}
				}
			}
		} else if (sym->id == 1 && sym->name[0] == ')' && sym->name[1] == '\0') {  // End
			if (spec->comment_start.name == NULL) { return; }

			buxn_asm_source_region_t region = spec->comment_start.region;
			if (spec->comment_first_token.name != NULL) {
				region.range.start = spec->comment_first_token.region.range.start;
			}

			if (spec->comment_last_token.name != NULL) {
				region.range.end = spec->comment_last_token.region.range.end;
			} else {
				region.range.end = sym->region.range.start;
			}

			if (
				spec->comment_kind == BUXN_ANNO_COMMENT_IS_TYPE
				&& spec->current_def.name != NULL
			) {
				spec->handler(
					spec->ctx,
					spec->current_def_addr, &spec->current_def,
					NULL,
					&region
				);
			} else if (
				spec->comment_kind == BUXN_ANNO_COMMENT_IS_CUSTOM_ANNOTATION
				&& spec->current_annotation != NULL
			) {
				switch (spec->current_annotation->type) {
					case BUXN_ANNOTATION_IMMEDIATE:
						spec->handler(
							spec->ctx,
							spec->last_rom_addr, &spec->last_rom_sym,
							spec->current_annotation,
							&region
						);
						break;
					case BUXN_ANNOTATION_PREFIX:
						// Defer
						spec->current_annotation->region = region;
						break;
					case BUXN_ANNOTATION_POSTFIX:
						spec->handler(
							spec->ctx,
							spec->current_def_addr, &spec->current_def,
							spec->current_annotation,
							&region
						);
						break;
				}
			}

			spec->comment_start.name = NULL;
			spec->comment_first_token.name = NULL;
			spec->comment_last_token.name = NULL;
			spec->current_def.name = NULL;
			spec->current_def_addr = 0;
			spec->current_annotation = NULL;
		} else {  // Intermediate
			if (spec->comment_first_token.name == NULL) {
				spec->comment_first_token = *sym;
			}
			spec->comment_last_token = *sym;

			if (
				spec->comment_kind == BUXN_ANNO_COMMENT_MIGHT_BE_TYPE
				&& (strcmp(sym->name, "--") == 0 || strcmp(sym->name, "->") == 0)
			) {
				spec->comment_kind = BUXN_ANNO_COMMENT_IS_TYPE;
			}
		}
	} else if (
		(sym_type == BUXN_ASM_SYM_MACRO || sym_type == BUXN_ASM_SYM_LABEL)
		&& !sym->name_is_generated
	) {
		spec->current_def = *sym;
		spec->current_def_addr = addr;
		// Apply deferred prefix annotations
		for (size_t i = 0; i < spec->num_annotations; ++i) {
			buxn_anno_t* anno = &spec->annotations[i];
			if (anno->region.filename != NULL) {
				spec->handler(spec->ctx, addr, sym, anno, &anno->region);
				anno->region.filename = NULL;
			}
		}
	}
}
