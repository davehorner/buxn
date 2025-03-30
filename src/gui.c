#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sokol_app.h>
#include <sokol_time.h>
#include <sokol_gfx.h>
#include <sokol_log.h>
#include <sokol_glue.h>
#include <sokol_gp.h>
#include "vm.h"
#include "devices/system.h"
#include "devices/sokol_console.h"
#include "devices/screen.h"
#include "devices/datetime.h"

#define FRAME_TIME_US (1000000.0 / 60.0)

typedef struct {
	buxn_system_t system;
	buxn_sokol_console_t console;
	buxn_screen_t* screen;
} devices_t;

typedef struct {
	sg_image gpu;
	uint32_t* cpu;
	size_t size;
} layer_texture_t;

static struct {
	int argc;
	const char** argv;
	buxn_vm_t* vm;
	devices_t devices;
	uint64_t last_frame;
	double frame_time_accumulator;

	sg_sampler sampler;
	layer_texture_t background_texture;
	layer_texture_t foreground_texture;
} app;

static void
init_layer_texture(
	layer_texture_t* texture,
	int width,
	int height,
	buxn_screen_info_t screen_info,
	const char* label
) {
	texture->cpu = malloc(screen_info.target_mem_size);
	texture->size = screen_info.target_mem_size;
	memset(texture->cpu, 0, screen_info.target_mem_size);
	texture->gpu = sg_make_image(&(sg_image_desc){
		.type = SG_IMAGETYPE_2D,
		.width = width,
		.height = height,
		.usage = SG_USAGE_STREAM,
		.label = label
	});
}

static void
cleanup_layer_texture(layer_texture_t* texture) {
	sg_destroy_image(texture->gpu);
	free(texture->cpu);
}

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	switch (buxn_device_id(address)) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, &devices->system, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_sokol_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_SCREEN:
			return buxn_screen_dei(vm, devices->screen, address);
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
		case BUXN_DEVICE_SCREEN:
			buxn_screen_deo(vm, devices->screen, address);
			break;
	}
}

static void
init(void) {
	stm_setup();

    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    sgp_setup(&(sgp_desc){ 0 });

	int width = sapp_width();
	int height = sapp_height();

	app.devices = (devices_t){ 0 };

	buxn_screen_info_t screen_info = buxn_screen_info(width, height);
	app.devices.screen = malloc(screen_info.screen_mem_size),
	memset(app.devices.screen, 0, sizeof(*app.devices.screen));
	buxn_screen_resize(app.devices.screen, width, height);

	init_layer_texture(&app.background_texture, width, height, screen_info, "background");
	init_layer_texture(&app.foreground_texture, width, height, screen_info, "foreground");

	app.vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	app.vm->userdata = &app.devices;
	app.vm->memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS;
	buxn_vm_reset(app.vm, BUXN_VM_RESET_ALL);
	buxn_sokol_console_init(app.vm, &app.devices.console, app.argc, app.argv);

	if (app.argc >= 2) {
		FILE* rom_file;
		if ((rom_file = fopen(app.argv[1], "rb")) != NULL) {
			uint8_t* read_pos = &app.vm->memory[BUXN_RESET_VECTOR];
			while (read_pos < app.vm->memory + app.vm->memory_size) {
				size_t num_bytes = fread(read_pos, 1, 1024, rom_file);
				if (num_bytes == 0) { break; }
				read_pos += num_bytes;
			}

			sapp_set_window_title(app.argv[1]);

			buxn_sokol_console_init(app.vm, &app.devices.console, app.argc - 2, app.argv + 2);
			buxn_vm_execute(app.vm, BUXN_RESET_VECTOR);
			buxn_sokol_console_send_args(app.vm, &app.devices.console);
		}
		fclose(rom_file);
	}

	app.last_frame = stm_now();
	app.frame_time_accumulator = FRAME_TIME_US;  // Render once
}

static void
cleanup(void) {
	free(app.devices.screen);
	cleanup_layer_texture(&app.foreground_texture);
	cleanup_layer_texture(&app.background_texture);
	free(app.vm);

	sgp_shutdown();
	sg_shutdown();
}

static void
frame(void) {
	if (buxn_system_exit_code(app.vm) > 0) { sapp_quit(); }

	uint64_t now = stm_now();
	double time_diff = stm_us(stm_diff(now, app.last_frame));
	app.last_frame = now;
	app.frame_time_accumulator += time_diff;

	bool draw_requested = false;
	while (app.frame_time_accumulator >= FRAME_TIME_US) {
		draw_requested = true;
		app.frame_time_accumulator -= FRAME_TIME_US;
		buxn_screen_update(app.vm);
	}

	if (draw_requested) {
		uint32_t palette[4];
		buxn_system_palette(app.vm, palette);

		if (buxn_screen_render(
			app.devices.screen,
			BUXN_SCREEN_LAYER_BACKGROUND,
			palette,
			app.background_texture.cpu
		)) {
			sg_update_image(
				app.background_texture.gpu,
				&(sg_image_data) {
					.subimage[0][0] = {
						.ptr = app.background_texture.cpu,
						.size = app.background_texture.size,
					},
				}
			);
		}

		palette[0] = 0; // Foreground treats color0 as transparent
		if (buxn_screen_render(
			app.devices.screen,
			BUXN_SCREEN_LAYER_FOREGROUND,
			palette,
			app.foreground_texture.cpu
		)) {
			sg_update_image(
				app.foreground_texture.gpu,
				&(sg_image_data) {
					.subimage[0][0] = {
						.ptr = app.foreground_texture.cpu,
						.size = app.foreground_texture.size,
					},
				}
			);
		}
	}

	int actual_width = sapp_width();
	int actual_height = sapp_height();
	int fb_width = app.devices.screen->width;
	int fb_height = app.devices.screen->height;

	float x_scale = (float)actual_width / (float)fb_width;
	float y_scale = (float)actual_height / (float)fb_height;
	float draw_scale = x_scale < y_scale ? x_scale : y_scale;
	float scaled_width = (float)fb_width * draw_scale;
	float scaled_height = (float)fb_height * draw_scale;
	float x_margin = ((float)actual_width - scaled_width) * 0.5f;
	float y_margin = ((float)actual_height - scaled_height) * 0.5f;

	sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
	{
		sgp_begin(actual_width, actual_height);
		{
			sgp_viewport(0, 0, actual_width, actual_height);
			sgp_project(0.f, (float)actual_width, 0.f, (float)actual_height);
			sgp_set_blend_mode(SGP_BLENDMODE_BLEND);

			sgp_set_image(0, app.background_texture.gpu);
			sgp_draw_filled_rect(x_margin, y_margin, scaled_width, scaled_height);

			sgp_set_image(0, app.foreground_texture.gpu);
			sgp_draw_filled_rect(x_margin, y_margin, scaled_width, scaled_height);
		}
		sgp_flush();
		sgp_end();
	}
	sg_end_pass();
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
        .width = 640,
        .height = 480,
        .sample_count = 1,
        .window_title = "buxn-gui",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
