#include "devices/file.h"
#include "vm.h"
#include <physfs.h>
#include <string.h>
#include <stdio.h>

// TODO: log error

typedef struct physfs_dir_buf_t {
	char** files;
	char** current;
} physfs_dir_buf_t;

static physfs_dir_buf_t dir_buf[BUXN_NUM_FILE_DEVICES] = { 0 };

buxn_file_handle_t*
buxn_file_fopen(struct buxn_vm_s* vm, const char* path, buxn_file_mode_t mode) {
	(void)vm;

	switch (mode) {
		case BUXN_FILE_MODE_READ:
			return PHYSFS_openRead(path);
		case BUXN_FILE_MODE_WRITE:
			return PHYSFS_openWrite(path);
		case BUXN_FILE_MODE_APPEND:
			return PHYSFS_openAppend(path);
	}
}

void
buxn_file_fclose(struct buxn_vm_s* vm, buxn_file_handle_t* handle) {
	(void)vm;

	PHYSFS_close(handle);
}

uint16_t
buxn_file_fread(struct buxn_vm_s* vm, buxn_file_handle_t* handle, void* buffer, uint16_t size) {
	(void)vm;

	if (PHYSFS_eof(handle)) { return 0; }

	PHYSFS_sint64 bytes_read = PHYSFS_readBytes(handle, buffer, size);
	return bytes_read >= 0 ? bytes_read : 0;
}

uint16_t
buxn_file_fwrite(struct buxn_vm_s* vm, buxn_file_handle_t* handle, const void* buffer, uint16_t size) {
	(void)vm;

	PHYSFS_sint64 bytes_written = PHYSFS_writeBytes(handle, buffer, size);
	return bytes_written >= 0 ? bytes_written : 0;
}

buxn_file_handle_t*
buxn_file_opendir(struct buxn_vm_s* vm, const char* path) {
	(void)vm;


	for (int i = 0; i < BUXN_NUM_FILE_DEVICES; ++i) {
		if (dir_buf[i].files == NULL) {
			char path_buf[BUXN_FILE_MAX_NAME + 2];
			snprintf(path_buf, sizeof(path_buf), "/%s", path);
			dir_buf[i].current = dir_buf[i].files = PHYSFS_enumerateFiles(
				strcmp(path, ".") == 0 ? "/" : path_buf
			);
			return &dir_buf[i];
		}
	}

	return NULL;
}

void
buxn_file_closedir(struct buxn_vm_s* vm, buxn_file_handle_t* handle) {
	(void)vm;

	physfs_dir_buf_t* dir_buf = handle;
	PHYSFS_freeList(dir_buf->files);
	dir_buf->files = NULL;
	dir_buf->current = NULL;
}

const char*
buxn_file_readdir(
	struct buxn_vm_s* vm,
	buxn_file_handle_t* handle,
	buxn_file_stat_t* stat
) {
	(void)vm;

	physfs_dir_buf_t* dir_buf = handle;
	const char* path = *dir_buf->current;
	if (path == NULL) { return NULL; }

	*stat = buxn_file_stat(vm, path);

	++dir_buf->current;
	return path;
}

bool
buxn_file_delete(struct buxn_vm_s* vm, const char* path) {
	(void)vm;
	return PHYSFS_delete(path);
}

buxn_file_stat_t
buxn_file_stat(struct buxn_vm_s* vm, const char* path) {
	(void)vm;

	char path_buf[BUXN_FILE_MAX_NAME + 2];
	snprintf(path_buf, sizeof(path_buf), "/%s", path);

	PHYSFS_Stat stat;
	if (strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
		return (buxn_file_stat_t){
			.type = BUXN_FILE_TYPE_DIRECTORY,
			.size = 0,
		};
	} else if (PHYSFS_stat(path_buf, &stat)) {
		if (stat.filetype == PHYSFS_FILETYPE_REGULAR) {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_REGULAR,
				.size = stat.filesize,
			};
		} else if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_DIRECTORY,
				.size = 0,
			};
		} else {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_INVALID,
				.size = 0,
			};
		}
	} else {
		return (buxn_file_stat_t){ .type = BUXN_FILE_TYPE_INVALID };
	}
}
