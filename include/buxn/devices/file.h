#ifndef BUXN_DEVICE_FILE_H
#define BUXN_DEVICE_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BUXN_FILE_MAX_PATH 1024
#define BUXN_FILE_MAX_NAME 256

struct buxn_vm_s;

typedef void buxn_file_handle_t;

typedef enum {
	BUXN_FILE_MODE_READ,
	BUXN_FILE_MODE_WRITE,
	BUXN_FILE_MODE_APPEND,
} buxn_file_mode_t;

typedef enum {
	BUXN_FILE_TYPE_INVALID,
	BUXN_FILE_TYPE_REGULAR,
	BUXN_FILE_TYPE_DIRECTORY,
} buxn_file_type_t;

typedef struct {
	buxn_file_type_t type;
	size_t size;
} buxn_file_stat_t;

typedef struct {
	buxn_file_handle_t* handle;
	buxn_file_mode_t mode;
	buxn_file_stat_t stat;
	uint16_t success;

	uint16_t read_dir_pos;
	uint16_t read_dir_len;
	char read_dir_buf[BUXN_FILE_MAX_NAME + 6];  // ---- NAME\n
} buxn_file_t;

uint8_t
buxn_file_dei(struct buxn_vm_s* vm, buxn_file_t* device, uint8_t* mem, uint8_t port);

void
buxn_file_deo(struct buxn_vm_s* vm, buxn_file_t* device, uint8_t* mem, uint8_t port);

// Must be provided by the host program

buxn_file_handle_t*
buxn_file_fopen(struct buxn_vm_s* vm, const char* path, buxn_file_mode_t mode);

void
buxn_file_fclose(struct buxn_vm_s* vm, buxn_file_handle_t* handle);

uint16_t
buxn_file_fread(struct buxn_vm_s* vm, buxn_file_handle_t* handle, void* buffer, uint16_t size);

uint16_t
buxn_file_fwrite(struct buxn_vm_s* vm, buxn_file_handle_t* handle, const void* buffer, uint16_t size);

buxn_file_handle_t*
buxn_file_opendir(struct buxn_vm_s* vm, const char* path);

void
buxn_file_closedir(struct buxn_vm_s* vm, buxn_file_handle_t* handle);

const char*
buxn_file_readdir(
	struct buxn_vm_s* vm,
	buxn_file_handle_t* handle,
	buxn_file_stat_t* stat
);

bool
buxn_file_delete(struct buxn_vm_s* vm, const char* path);

buxn_file_stat_t
buxn_file_stat(struct buxn_vm_s* vm, const char* path);

#endif
