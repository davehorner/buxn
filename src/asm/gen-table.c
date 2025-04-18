#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <chibihash64.h>
#include "../vm/opcodes.h"

#define OPCODE_TABLE_ENTRY(NAME, VALUE) [VALUE] = STRINGIFY(NAME),
#define STRINGIFY(NAME) STRINGIFY_1(NAME)
#define STRINGIFY_1(NAME) #NAME
#define NUM_OPCODES 256

static const char* opcode_names[NUM_OPCODES] = {
	BUXN_OPCODE_DISPATCH(OPCODE_TABLE_ENTRY)
};

typedef struct {
	int next_seed;
	uint8_t index;
	uint8_t num_opcodes;
	uint8_t opcodes[NUM_OPCODES];
} intermediate_slot_t;

typedef struct {
	bool used;
	uint8_t opcode;
} final_slot_t;

static int
cmp_slot(const void* lhs, const void* rhs) {
	const intermediate_slot_t* lhs_slot = lhs;
	const intermediate_slot_t* rhs_slot = rhs;

	return (int)rhs_slot->num_opcodes - (int)lhs_slot->num_opcodes;
}

int
main(int argc, const char* argv[]) {
	(void)argc;
	(void)argv;

	intermediate_slot_t intermediate_table[NUM_OPCODES];
	for (int i = 0; i < NUM_OPCODES; ++i) {
		intermediate_table[i].index = i;
		intermediate_table[i].num_opcodes = 0;
		intermediate_table[i].next_seed = 0;
	}

	for (int i = 0; i < NUM_OPCODES; ++i) {
		uint64_t hash = chibihash64(opcode_names[i], strlen(opcode_names[i]), 0);
		uint64_t slot = hash & 0xff;

		uint8_t num_opcodes = intermediate_table[slot].num_opcodes++;
		intermediate_table[slot].opcodes[num_opcodes] = i;
	}

	qsort(intermediate_table, NUM_OPCODES, sizeof(intermediate_slot_t), cmp_slot);
	/*for (int i = 0; i < NUM_OPCODES; ++i) {*/
		/*printf("len(%d) = %d\n", intermediate_table[i].index, intermediate_table[i].num_opcodes);*/
	/*}*/

	final_slot_t final_table[NUM_OPCODES] = { 0 };

	for (int i = 0; i < NUM_OPCODES; ++i) {
		intermediate_slot_t* slot = &intermediate_table[i];

		if (slot->num_opcodes > 1) {
			// Bruteforce seed until everything fits
			int num_opcodes = slot->num_opcodes;
			for (int seed = 1;; ++seed) {
				bool collided = false;
				for (int j = 0; j < num_opcodes; ++j) {
					uint8_t opcode = slot->opcodes[j];
					const char* opcode_name = opcode_names[opcode];
					uint64_t hash = chibihash64(opcode_name, strlen(opcode_name), seed);
					uint64_t final_slot = hash & 0xff;
					if (final_table[final_slot].used) {
						collided = true;
						break;
					} else {
						final_table[final_slot].used = true;
						final_table[final_slot].opcode = opcode;
					}
				}

				// Cleanup before retry
				if (collided) {
					for (int j = 0; j < num_opcodes; ++j) {
						uint8_t opcode = slot->opcodes[j];
						const char* opcode_name = opcode_names[opcode];
						uint64_t hash = chibihash64(opcode_name, strlen(opcode_name), seed);
						uint64_t final_slot = hash & 0xff;

						if (
							final_table[final_slot].used
							&& final_table[final_slot].opcode == opcode
						) {
							final_table[final_slot].used = false;
						}
					}
				} else {
					slot->next_seed = seed;
					break;
				}
			}
		} else if (slot->num_opcodes == 1) {
			// Place directly into a free slot
			for (int j = 0; j < NUM_OPCODES; ++j) {
				if (!final_table[j].used) {
					final_table[j].used = true;
					final_table[j].opcode = slot->opcodes[0];
					slot->next_seed = -j - 1;
					break;
				}
			}
		} else {
			break;
		}
	}

	int seed_table[NUM_OPCODES] = { 0 };

	for (int i = 0; i < NUM_OPCODES; ++i) {
		seed_table[intermediate_table[i].index] = intermediate_table[i].next_seed;
	}

	// Sanity check
	final_slot_t check_table[NUM_OPCODES] = { 0 };
	for (int i = 0; i < NUM_OPCODES; ++i) {
		const char* opcode_name = opcode_names[i];
		size_t len = strlen(opcode_name);
		uint64_t intermediate_hash = chibihash64(opcode_name, len, 0);
		uint64_t intermediate_slot = intermediate_hash & 0xff;
		int seed = seed_table[intermediate_slot];
		int final_slot;
		if (seed < 0) {
			final_slot = -seed - 1;
		} else {
			uint64_t final_hash = chibihash64(opcode_name, len, seed);
			final_slot = final_hash & 0xff;
		}

		if (check_table[final_slot].used) {
			fprintf(stderr, "Slot %d has both %d and %d\n", final_slot, i, check_table[final_slot].opcode);
			abort();
		} else {
			check_table[final_slot].used = true;
			check_table[final_slot].opcode = i;
		}
	}

	// Generate source
	printf("#ifndef BUXN_ASM_OPCODE_HASH_H\n");
	printf("#define BUXN_ASM_OPCODE_HASH_H\n\n");
	printf("/* Generated using src/asm/gen-table.c */\n");
	printf("/* cc -Wall -Wextra -Werror -pedantic -Ideps/chibihash src/asm/gen-table.c -o /tmp/gen-table && /tmp/gen-table */\n");
	printf("static const int buxn_opcode_hash_seeds[%d] = {", NUM_OPCODES);
	for (int i = 0; i < NUM_OPCODES; ++i) {
		if (i % 8 == 0) {
			printf("\n    ");
		}

		printf("0x%08x,", seed_table[i]);

		if (i % 8 != 7) {
			printf(" ");
		}
	}
	printf("\n};\n\n");

	printf("static const uint8_t buxn_hashed_opcode_values[%d] = {", NUM_OPCODES);
	for (int i = 0; i < NUM_OPCODES; ++i) {
		if (i % 8 == 0) {
			printf("\n    ");
		}

		printf("0x%02x,", check_table[i].opcode);

		if (i % 8 != 7) {
			printf(" ");
		}
	}
	printf("\n};\n\n");

	printf("static const char* const buxn_hashed_opcode_names[%d] = {", NUM_OPCODES);
	for (int i = 0; i < NUM_OPCODES; ++i) {
		if (i % 8 == 0) {
			printf("\n    ");
		}

		char buf[9];
		sprintf(buf, "\"%s\"", opcode_names[check_table[i].opcode]);
		printf("%8s,", buf);

		if (i % 8 != 7) {
			printf(" ");
		}
	}
	printf("\n};\n\n");
	printf("#endif\n");

	return 0;
}
