#include <buxn/asm/annotation.h>
#include <string.h>

void
buxn_anno_handle_symbol(buxn_anno_spec_t* spec, const buxn_asm_sym_t* sym) {
	if (sym->type == BUXN_ASM_SYM_COMMENT) {
		if (sym->id == 0) {  // Start
			spec->comment_start = *sym;
			spec->comment_first_token.name = NULL;
			spec->comment_last_token.name = NULL;

			spec->current_annotation = NULL;
			if (sym->name[1] == '\0') {  // A bare '('
				spec->comment_kind = spec->current_sym.name != NULL
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
				&& spec->current_sym.name != NULL
			) {
				buxn_anno_handle_type(spec->ctx, &spec->current_sym, &region);
			} else if (
				spec->comment_kind == BUXN_ANNO_COMMENT_IS_CUSTOM_ANNOTATION
				&& spec->current_annotation != NULL
			) {
				switch (spec->current_annotation->type) {
					case BUXN_ANNOTATION_IMMEDIATE:
						buxn_anno_handle_custom(
							spec->ctx,
							spec->current_annotation,
							NULL, &region
						);
						break;
					case BUXN_ANNOTATION_PREFIX:
						// Defer
						spec->current_annotation->region = region;
						break;
					case BUXN_ANNOTATION_POSTFIX:
						buxn_anno_handle_custom(
							spec->ctx,
							spec->current_annotation,
							&spec->current_sym, &region
						);
						break;
				}
			}

			spec->comment_start.name = NULL;
			spec->comment_first_token.name = NULL;
			spec->comment_last_token.name = NULL;
			spec->current_sym.name = NULL;
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
		(sym->type == BUXN_ASM_SYM_MACRO || sym->type == BUXN_ASM_SYM_LABEL)
		&& !sym->name_is_generated
	) {
		spec->current_sym = *sym;
		// Apply deferred prefix annotations
		for (size_t i = 0; i < spec->num_annotations; ++i) {
			buxn_anno_t* anno = &spec->annotations[i];
			if (anno->region.filename != NULL) {
				buxn_anno_handle_custom(
					spec->ctx,
					anno,
					sym, &anno->region
				);
				anno->region.filename = NULL;
			}
		}
	}
}
