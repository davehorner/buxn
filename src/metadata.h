#ifndef BUXN_METADATA_H
#define BUXN_METADATA_H

#include <stdint.h>
#include <stddef.h>

struct buxn_vm_s;

#define BUXN_METADATA_EXT_DEVICE_MASK 0x41
#define BUXN_METADATA_EXT_ICON_CHR    0x83
#define BUXN_METADATA_EXT_ICON_ICN    0x88
#define BUXN_METADATA_EXT_MANIFEST    0xa0

typedef struct {
	uint8_t version;
	const char* content;
	int content_len;

	uint8_t* extension_address;
	uint8_t num_extensions;
} buxn_metadata_t;

typedef struct {
	uint8_t id;
	uint16_t value;
} buxn_metadata_ext_t;

buxn_metadata_t
buxn_metadata_parse_from_memory(struct buxn_vm_s* vm, uint16_t address);

buxn_metadata_t
buxn_metadata_parse_from_rom(uint8_t* rom, size_t size);

buxn_metadata_ext_t
buxn_metadata_get_ext(const buxn_metadata_t* metadata, uint8_t index);

#endif
