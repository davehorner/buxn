#include <stdlib.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_log.h>
#include <sokol_glue.h>
#include <sokol_gp.h>
#include "vm.h"
#include "devices/sokol_console.h"
#include "devices/system.h"
#include "devices/datetime.h"

#include <math.h>

typedef struct {
	buxn_system_t system;
	buxn_sokol_console_t console;
} devices_t;

typedef struct {
	sg_image image;
	sg_color* data;
} layer_t;

static struct {
	int argc;
	const char** argv;
	buxn_vm_t* vm;
	devices_t devices;
} app;

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	switch (buxn_device_id(address)) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, &devices->system, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_sokol_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	switch (buxn_device_id(address)) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, &devices->system, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_sokol_console_deo(vm, &devices->console, address);
			break;
	}
}

static void
init(void) {
	app.devices = (devices_t){ 0 };
	app.vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	app.vm->userdata = &app.devices;
	app.vm->memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS;
	buxn_vm_reset(app.vm, BUXN_VM_RESET_ALL);
	buxn_sokol_console_init(app.vm, &app.devices.console, app.argc, app.argv);

    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    sgp_setup(&(sgp_desc){ 0 });
}

static void
cleanup(void) {
	sgp_shutdown();
	sg_shutdown();

	free(app.vm);
}

static void
frame(void) {
	// Get current window size.
	int width = sapp_width(), height = sapp_height();
	float ratio = width/(float)height;

	// Begin recording draw commands for a frame buffer of size (width, height).
	sgp_begin(width, height);
	// Set frame buffer drawing region to (0,0,width,height).
	sgp_viewport(0, 0, width, height);
	// Set drawing coordinate space to (left=-ratio, right=ratio, top=1, bottom=-1).
	sgp_project(-ratio, ratio, 1.0f, -1.0f);

	// Clear the frame buffer.
	sgp_set_color(0.5f, 0.5f, 0.5f, 1.0f);
	sgp_clear();

	// Draw an animated rectangle that rotates and changes its colors.
	float time = sapp_frame_count() * sapp_frame_duration();
	float r = sinf(time)*0.5+0.5, g = cosf(time)*0.5+0.5;
	sgp_set_color(r, g, 0.3f, 1.0f);
	sgp_rotate_at(time, 0.0f, 0.0f);
	sgp_draw_filled_rect(-0.5f, -0.5f, 1.0f, 1.0f);

	// Begin a render pass.
	sg_pass pass = { .swapchain = sglue_swapchain() };
	sg_begin_pass(&pass);
	// Dispatch all draw commands to Sokol GFX.
	sgp_flush();
	// Finish a draw command queue, clearing it.
	sgp_end();
	// End render pass.
	sg_end_pass();
	// Commit Sokol render.
	sg_commit();
}

sapp_desc
sokol_main(int argc, char* argv[]) {
   	app.argc = argc;
   	app.argv = (const char**)argv;

    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = 800,
        .height = 600,
        .sample_count = 4,
        .window_title = "buxn-gui",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
