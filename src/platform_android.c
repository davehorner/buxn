#include "platform.h"
#include <blog.h>
#include <physfs.h>
#include <sokol_app.h>
#include <android/native_activity.h>

static blog_android_logger_options_t options = { 0 };

void
platform_init_log(void) {
	options.tag = "buxn";
	blog_add_android_logger(BLOG_LEVEL_DEBUG, &options);
}

void
platform_init_fs(int argc, const char* argv[]) {
	(void)argc;
	(void)argv;

	const ANativeActivity* activity = sapp_android_get_native_activity();

	JNIEnv* jenv;
	JavaVM* jvm = activity->vm;
    (*jvm)->AttachCurrentThread(jvm, &jenv, NULL);
	PHYSFS_init((void*)&(struct PHYSFS_AndroidInit){
		.jnienv = jenv,
		.context = activity->clazz,
	});
    (*jvm)->DetachCurrentThread(jvm);

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

void
platform_resize_window(uint16_t width, uint16_t height) {
	(void)width;
	(void)height;
}
