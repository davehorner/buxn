#include "platform.h"
#include <blog.h>
#include <physfs.h>
#include <sokol_app.h>
#include <android/native_activity.h>

static struct {
	blog_android_logger_options_t log_options;
} platform_android = { 0 };

static int64_t
physfs_read(void* handle, void* buffer, uint64_t num_bytes) {
	return PHYSFS_readBytes(handle, buffer, num_bytes);
}

void
platform_parse_args(args_t* args) {
	(void)args;  // No op
}

void
platform_init_log(void) {
	platform_android.log_options.tag = "buxn";
	blog_add_android_logger(BLOG_LEVEL_DEBUG, &platform_android.log_options);
}

void
platform_init_fs(void) {
	const ANativeActivity* activity = sapp_android_get_native_activity();
	PHYSFS_init((void*)&(struct PHYSFS_AndroidInit){
		.jnienv = activity->env,
		.context = activity->clazz,
	});

	const char* base_dir = PHYSFS_getBaseDir();
	if (!PHYSFS_mount(base_dir, "/", 1)) {
		BLOG_ERROR("Error while mounting apk: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PHYSFS_setRoot(base_dir, "/assets");
	// The actual valuee doesn't matter on Android as it will alawys resolve
	// to the data directory.
	const char* write_dir = PHYSFS_getPrefDir("bullno1.com", "buxn");
	PHYSFS_setWriteDir(write_dir);
	PHYSFS_mount(write_dir, "/", 1);
}

bool
platform_open_boot_rom(stream_t* stream) {
	PHYSFS_File* file = PHYSFS_openRead("boot.rom");
	if (file == NULL) {
		BLOG_ERROR("Could not open rom file: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return false;
	}

	stream->handle = file;
	stream->read = physfs_read;
	return true;
}

const char*
platform_stream_error(stream_t stream) {
	(void)stream;
	return PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
}

void
platform_close_stream(stream_t stream) {
	PHYSFS_close(stream.handle);
}

void
platform_resize_window(uint16_t width, uint16_t height) {
	(void)width;
	(void)height;
}
