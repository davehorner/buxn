#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sokol_app.h>
#include <sokol_audio.h>
#include <sokol_time.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_gp.h>
#include <blog.h>
#include <physfs.h>
#include <math.h>
#include <threads.h>
#include <stdatomic.h>
#include "vm.h"
#include "metadata.h"
#include "devices/system.h"
#include "devices/console.h"
#include "devices/screen.h"
#include "devices/mouse.h"
#include "devices/controller.h"
#include "devices/datetime.h"
#include "devices/audio.h"
#include "devices/file.h"
#include "platform.h"

#define FRAME_TIME_US (1000000.0 / 60.0)
#define CONSOLE_BUFFER_SIZE 256

typedef struct {
	buxn_console_t console;
	buxn_mouse_t mouse;
	buxn_controller_t controller;
	buxn_audio_t audio[BUXN_NUM_AUDIO_DEVICES];
	buxn_screen_t* screen;
	buxn_file_t file[BUXN_NUM_FILE_DEVICES];
} devices_t;

typedef struct {
	int pos;
	char data[CONSOLE_BUFFER_SIZE];
} console_buf_t;

typedef struct {
	sg_image gpu;
	uint32_t* cpu;
	size_t size;
} layer_texture_t;

typedef struct {
	int actual_width;
	int actual_height;
	float draw_scale;
	float scaled_width;
	float scaled_height;
	float x_margin;
	float y_margin;
} draw_info_t;

static struct {
	args_t args;

	buxn_vm_t* vm;
	devices_t devices;
	uint64_t last_frame;
	double frame_time_accumulator;

	console_buf_t console_out_buf;
	console_buf_t console_err_buf;

	sg_sampler sampler;
	layer_texture_t background_texture;
	layer_texture_t foreground_texture;

	mtx_t audio_lock;
	atomic_int audio_finished_count[BUXN_NUM_AUDIO_DEVICES];
	int audio_finished_ack[BUXN_NUM_AUDIO_DEVICES];
	buxn_audio_message_t incoming_audio[BUXN_NUM_AUDIO_DEVICES];

	bool should_set_icon;
	uint8_t paletted_icon[24 * 24];
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
	BLOG_INFO("Created layer texture %s with size %dx%d", label, width, height);
}

static void
cleanup_layer_texture(layer_texture_t* texture) {
	sg_destroy_image(texture->gpu);
	free(texture->cpu);
}

static void
sokol_log(const char* tag, uint32_t log_level, uint32_t log_item, const char* message, uint32_t line_nr, const char* filename, void* user_data) {
	(void)user_data;

	blog_level_t blog_level = BLOG_LEVEL_INFO;
	switch (log_level) {
		case 0: blog_level = BLOG_LEVEL_FATAL;
		case 1: blog_level = BLOG_LEVEL_ERROR;
		case 2: blog_level = BLOG_LEVEL_WARN;
		default: blog_level = BLOG_LEVEL_INFO;
	}

	blog_write(blog_level, filename, line_nr, "%s(%d): %s", tag, log_item, message);
}

// https://wiki.xxiivv.com/site/chr_format.html
static void
draw_chr(uint8_t* dst, int x, int y, uint16_t addr) {
	(void)dst;
	for(int v = 0; v < 8; v++) {
		for(int h = 0; h < 8; h++) {
			int ch1 = ((app.vm->memory[(addr + v) & 0xffff] >> h) & 0x1);
			int ch2 = (((app.vm->memory[(addr + v + 8) & 0xffff] >> h) & 0x1) << 1);

			int element = (y + v) * 24 + (x + 7 - h);
			dst[element] = ch1 + ch2;
		}
	}
}

static void
apply_metadata(buxn_metadata_t metadata) {
	char* ch = metadata.content;
	while (ch < metadata.content + metadata.content_len && *ch != '\n') {
		++ch;
	}
	// It is VM so the memory is writable
	// We can avoid allocation
	char old_char = *ch;
	*ch = '\0';
	sapp_set_window_title(metadata.content);
	*ch = old_char;

	for (int i = 0; i < metadata.num_extensions; ++i) {
		buxn_metadata_ext_t ext = buxn_metadata_get_ext(&metadata, i);

		switch (ext.id) {
			case BUXN_METADATA_EXT_ICON_CHR: {
				for (int y = 0; y < 3; ++y) {
					for (int x = 0; x < 3; ++x) {
						draw_chr(
							app.paletted_icon,
							x * 8, y * 8,
							ext.value + (x + (y * 3)) * 16
						);
					}
				}

				app.should_set_icon = true;
			} break;
		}
	}
}

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_SCREEN:
			return buxn_screen_dei(vm, devices->screen, address);
		case BUXN_DEVICE_AUDIO_0:
		case BUXN_DEVICE_AUDIO_1:
		case BUXN_DEVICE_AUDIO_2:
		case BUXN_DEVICE_AUDIO_3:
			return buxn_audio_dei(
				vm,
				devices->audio + (device_id - BUXN_DEVICE_AUDIO_0) / (BUXN_DEVICE_AUDIO_1 - BUXN_DEVICE_AUDIO_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
		case BUXN_DEVICE_MOUSE:
			return buxn_mouse_dei(vm, &devices->mouse, address);
		case BUXN_DEVICE_CONTROLLER:
			return buxn_controller_dei(vm, &devices->controller, address);
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			return buxn_file_dei(
				vm,
				devices->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &devices->console, address);
			break;
		case BUXN_DEVICE_SCREEN:
			buxn_screen_deo(vm, devices->screen, address);
			break;
		case BUXN_DEVICE_AUDIO_0:
		case BUXN_DEVICE_AUDIO_1:
		case BUXN_DEVICE_AUDIO_2:
		case BUXN_DEVICE_AUDIO_3:
			buxn_audio_deo(
				vm,
				devices->audio + (device_id - BUXN_DEVICE_AUDIO_0) / (BUXN_DEVICE_AUDIO_1 - BUXN_DEVICE_AUDIO_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
			break;
		case BUXN_DEVICE_MOUSE:
			buxn_mouse_deo(vm, &devices->mouse, address);
			break;
		case BUXN_DEVICE_CONTROLLER:
			buxn_controller_deo(vm, &devices->controller, address);
			break;
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			buxn_file_deo(
				vm,
				devices->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
			break;
	}
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
	(void)vm;
	(void)value;
}

void
buxn_system_set_metadata(buxn_vm_t* vm, uint16_t address) {
	buxn_metadata_t metadata = buxn_metadata_parse_from_memory(vm, address);
	if (metadata.content == NULL) {
		BLOG_WARN("ROM tried to set invalid metadata");
		return;
	}

	apply_metadata(metadata);
}

void
buxn_system_theme_changed(buxn_vm_t* vm) {
	(void)vm;
	buxn_screen_force_refresh(app.devices.screen);
}

static void
get_draw_info(draw_info_t* out) {
	int actual_width = sapp_width();
	int actual_height = sapp_height();
	int fb_width = app.devices.screen->width;
	int fb_height = app.devices.screen->height;

	float x_scale = (float)actual_width / (float)fb_width;
	float y_scale = (float)actual_height / (float)fb_height;
	float draw_scale = x_scale < y_scale ? x_scale : y_scale;
	float scaled_width = (float)fb_width * draw_scale;
	float scaled_height = (float)fb_height * draw_scale;
	float x_margin = floorf(((float)actual_width - scaled_width) * 0.5f);
	float y_margin = floorf(((float)actual_height - scaled_height) * 0.5f);

	*out = (draw_info_t){
		.actual_width = actual_width,
		.actual_height = actual_height,
		.draw_scale = draw_scale,
		.scaled_width = scaled_width,
		.scaled_height = scaled_height,
		.x_margin = x_margin,
		.y_margin = y_margin,
	};
}

static inline void
buxn_console_putc(
	blog_level_t level,
	console_buf_t* buffer,
	char ch
) {
	bool should_flush = false;
	if (ch != '\n') {
		buffer->data[buffer->pos++] = ch;
		should_flush = buffer->pos >= (int)(sizeof(buffer->data) - 1);
	} else {
		should_flush = true;
	}

	if (should_flush) {
		buffer->data[buffer->pos] = '\0';
		blog_write(level, __FILE__, __LINE__, "%s", buffer->data);
		buffer->pos = 0;
	}
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	buxn_console_putc(BLOG_LEVEL_INFO, &app.console_out_buf, c);
}

extern void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	buxn_console_putc(BLOG_LEVEL_ERROR, &app.console_err_buf, c);
}

buxn_screen_t*
buxn_screen_request_resize(
	struct buxn_vm_s* vm,
	buxn_screen_t* screen,
	uint16_t width, uint16_t height
) {
	(void)vm;
	cleanup_layer_texture(&app.background_texture);
	cleanup_layer_texture(&app.foreground_texture);

	BLOG_INFO("Received resize request to: %dx%d", width, height);

	buxn_screen_info_t screen_info = buxn_screen_info(width, height);
	init_layer_texture(&app.background_texture, width, height, screen_info, "uxn.screen.background");
	init_layer_texture(&app.foreground_texture, width, height, screen_info, "uxn.screen.foreground");
	screen = realloc(screen, screen_info.screen_mem_size);
	buxn_screen_resize(screen, width, height);
	app.devices.screen = screen;

	platform_resize_window(width, height);

	return screen;
}

static bool
load_boot_rom(void) {
	stream_t stream;
	if (!platform_open_boot_rom(&stream)) {
		return false;
	}

	uint8_t* read_pos = &app.vm->memory[BUXN_RESET_VECTOR];
	uint64_t mem_left = app.vm->memory_size - BUXN_RESET_VECTOR;
	int64_t bytes_read;
	while (
		mem_left > 0
		&& (bytes_read = stream.read(stream.handle, read_pos, mem_left)) > 0
	) {
		mem_left -= bytes_read;
		read_pos += bytes_read;
	}

	if (bytes_read < 0) {
		BLOG_ERROR("Error while loading rom: %s", platform_stream_error(stream));
		return false;
	}

	if (mem_left == 0 && bytes_read > 0) {
		BLOG_ERROR("ROM is too large");
		return false;
	}

	BLOG_DEBUG("Loaded rom: %zu bytes", read_pos - &app.vm->memory[BUXN_RESET_VECTOR]);
	platform_close_stream(stream);

	buxn_metadata_t metadata = buxn_metadata_parse_from_rom(
		&app.vm->memory[BUXN_RESET_VECTOR], app.vm->memory_size - 256
	);

	if (metadata.content_len != 0) {
		apply_metadata(metadata);
	}

	BLOG_DEBUG("Executing reset vector");
	buxn_console_init(app.vm, &app.devices.console, app.args.argc, app.args.argv);
	buxn_vm_execute(app.vm, BUXN_RESET_VECTOR);
	buxn_console_send_args(app.vm, &app.devices.console);
	buxn_console_send_input(app.vm, &app.devices.console, '\n');
	buxn_console_send_input_end(app.vm, &app.devices.console);

	return true;
}

void
buxn_audio_send(buxn_vm_t* vm, const buxn_audio_message_t* message) {
	(void)vm;
	if (mtx_trylock(&app.audio_lock) == thrd_success) {
		int device_index = message->device - app.devices.audio;
		app.incoming_audio[device_index] = *message;
		mtx_unlock(&app.audio_lock);
	} else {
		BLOG_WARN("Dropped audio sample");
	}
}

static void
audio_callback(float* buffer, int num_frames, int num_channels) {
	// Process incoming audio
	if (mtx_trylock(&app.audio_lock) == thrd_success) {
		for (int i = 0; i < BUXN_NUM_AUDIO_DEVICES; ++i) {
			buxn_audio_message_t* message = &app.incoming_audio[i];
			if (message->device != NULL) {
				buxn_audio_receive(message);
				message->device = NULL;
			}
		}
		mtx_unlock(&app.audio_lock);
	} else {
		BLOG_WARN("Skipped audio sample");
	}

	// Render audio
	memset(buffer, 0, sizeof(float) * num_frames * num_channels);
	for (int i = 0; i < BUXN_NUM_AUDIO_DEVICES; ++i) {
		if (buxn_audio_render(&app.devices.audio[i], buffer, num_frames, num_channels) == BUXN_AUDIO_FINISHED) {
			atomic_fetch_add_explicit(&app.audio_finished_count[i], 1, memory_order_relaxed);
		}
	}
}

static void
init(void) {
	app.devices = (devices_t){ 0 };

	stm_setup();

	sg_setup(&(sg_desc){
		.environment = sglue_environment(),
		.logger.func = sokol_log,
	});
	sgp_setup(&(sgp_desc){ 0 });

	mtx_init(&app.audio_lock, mtx_plain);
	mtx_lock(&app.audio_lock);
	saudio_setup(&(saudio_desc){
		.num_channels = BUXN_AUDIO_PREFERRED_NUM_CHANNELS,
		.sample_rate = BUXN_AUDIO_PREFERRED_SAMPLE_RATE,
		.stream_cb = audio_callback,
		.logger.func = sokol_log,
	});
	int audio_sample_rate = saudio_sample_rate();
	for (int i = 0; i < BUXN_NUM_AUDIO_DEVICES; ++i) {
		app.devices.audio[i].sample_frequency = audio_sample_rate;
	}
	mtx_unlock(&app.audio_lock);
	BLOG_INFO(
		"Audio initalized with sample rate %d Hz and %d channel(s)",
		audio_sample_rate,
		saudio_channels()
	);

	int width = sapp_width();
	int height = sapp_height();

	buxn_screen_info_t screen_info = buxn_screen_info(width, height);
	app.devices.screen = malloc(screen_info.screen_mem_size),
	memset(app.devices.screen, 0, sizeof(*app.devices.screen));
	buxn_screen_resize(app.devices.screen, width, height);

	init_layer_texture(&app.background_texture, width, height, screen_info, "uxn.screen.background");
	init_layer_texture(&app.foreground_texture, width, height, screen_info, "uxn.screen.foreground");
	app.sampler = sg_make_sampler(&(sg_sampler_desc){
		.min_filter = SG_FILTER_NEAREST,
		.mag_filter = SG_FILTER_NEAREST,
		.wrap_u = SG_WRAP_CLAMP_TO_EDGE,
		.wrap_v = SG_WRAP_CLAMP_TO_EDGE,
		.label = "uxn.screen",
	});

	app.vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	app.vm->userdata = &app.devices;
	app.vm->memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS;
	buxn_vm_reset(app.vm, BUXN_VM_RESET_ALL);

	if (!load_boot_rom()) {
		BLOG_FATAL("Could not load boot rom");
		sapp_quit();
	}

	app.last_frame = stm_now();
	app.frame_time_accumulator = FRAME_TIME_US;  // Render once
}

static void
cleanup(void) {
	free(app.devices.screen);
	sg_destroy_sampler(app.sampler);
	cleanup_layer_texture(&app.foreground_texture);
	cleanup_layer_texture(&app.background_texture);

	saudio_shutdown();
	mtx_destroy(&app.audio_lock);

	free(app.vm);

	sgp_shutdown();
	sg_shutdown();

	PHYSFS_deinit();
}

static void
frame(void) {
	if (buxn_system_exit_code(app.vm) > 0) { sapp_quit(); }

	for (int i = 0; i < BUXN_NUM_AUDIO_DEVICES; ++i) {
		int num_finished = atomic_load_explicit(&app.audio_finished_count[i], memory_order_relaxed);
		if (num_finished != app.audio_finished_ack[i]) {
			buxn_audio_notify_finished(app.vm, BUXN_DEVICE_AUDIO_0 + i);
			app.audio_finished_ack[i] = num_finished;
		}
	}

	uint64_t now = stm_now();
	double time_diff = stm_us(stm_diff(now, app.last_frame));
	app.last_frame = now;
	app.frame_time_accumulator += time_diff;

	bool should_redraw = app.frame_time_accumulator >= FRAME_TIME_US;
	while (app.frame_time_accumulator >= FRAME_TIME_US) {
		app.frame_time_accumulator -= FRAME_TIME_US;
		buxn_screen_update(app.vm);
	}

	if (should_redraw) {
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

		if (app.should_set_icon) {
			BLOG_DEBUG("Changing app icon");
			uint32_t rendered_icon[24 * 24];

			for (int i = 0; i < (int)sizeof(app.paletted_icon); ++i) {
				rendered_icon[i] = palette[app.paletted_icon[i]];
			}
			sapp_set_icon(&(sapp_icon_desc){
				.images[0] = {
					.width = 24,
					.height = 24,
					.pixels = {
						.ptr = rendered_icon,
						.size = sizeof(rendered_icon),
					},
				}
			});
			app.should_set_icon = false;
		}
	}

	draw_info_t draw_info;
	get_draw_info(&draw_info);

	sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
	{
		sgp_begin(draw_info.actual_width, draw_info.actual_height);
		{
			sgp_viewport(0, 0, draw_info.actual_width, draw_info.actual_height);
			sgp_project(0.f, (float)draw_info.actual_width, 0.f, (float)draw_info.actual_height);
			sgp_set_blend_mode(SGP_BLENDMODE_BLEND);
			sgp_clear();

			sgp_set_sampler(0, app.sampler);

			sgp_set_image(0, app.background_texture.gpu);
			sgp_draw_filled_rect(draw_info.x_margin, draw_info.y_margin, draw_info.scaled_width, draw_info.scaled_height);

			sgp_set_image(0, app.foreground_texture.gpu);
			sgp_draw_filled_rect(draw_info.x_margin, draw_info.y_margin, draw_info.scaled_width, draw_info.scaled_height);
		}
		sgp_flush();
		sgp_end();
	}
	sg_end_pass();
	sg_commit();
}

static float
clamp(float val, float min, float max) {
	if (val < min) {
		return min;
	} else if (val > max) {
		return max;
	} else {
		return val;
	}
}

static void
event(const sapp_event* event) {
	bool update_mouse = false;
	switch (event->type) {
		case SAPP_EVENTTYPE_MOUSE_UP:
		case SAPP_EVENTTYPE_MOUSE_DOWN: {
			int button;
			switch (event->mouse_button) {
				case SAPP_MOUSEBUTTON_LEFT:
					button = 0;
					break;
				case SAPP_MOUSEBUTTON_RIGHT:
					button = 2;
					break;
				case SAPP_MOUSEBUTTON_MIDDLE:
					button = 1;
					break;
				default:
					button = -1;
					break;
			}
			if (button >= 0) {
				buxn_mouse_set_button(
					&app.devices.mouse,
					button,
					event->type == SAPP_EVENTTYPE_MOUSE_DOWN
				);
				update_mouse = true;
			}
		} break;
		case SAPP_EVENTTYPE_MOUSE_SCROLL:
			app.devices.mouse.scroll_x = (int16_t)event->scroll_x;
			app.devices.mouse.scroll_y = -(int16_t)event->scroll_y;
			update_mouse = true;
			break;
		case SAPP_EVENTTYPE_MOUSE_MOVE: {
			draw_info_t draw_info;
			get_draw_info(&draw_info);

			float mouse_x = (float)(event->mouse_x - draw_info.x_margin) / draw_info.draw_scale;
			float mouse_y = (float)(event->mouse_y - draw_info.y_margin) / draw_info.draw_scale;

			app.devices.mouse.x = (uint16_t)clamp(mouse_x, 0.f, (float)app.devices.screen->width);
			app.devices.mouse.y = (uint16_t)clamp(mouse_y, 0.f, (float)app.devices.screen->height);
			update_mouse = true;
		} break;
		case SAPP_EVENTTYPE_TOUCHES_BEGAN:
		case SAPP_EVENTTYPE_TOUCHES_ENDED: {
			draw_info_t draw_info;
			get_draw_info(&draw_info);

			float mouse_x = (float)(event->touches[0].pos_x - draw_info.x_margin) / draw_info.draw_scale;
			float mouse_y = (float)(event->touches[0].pos_y - draw_info.y_margin) / draw_info.draw_scale;

			app.devices.mouse.x = (uint16_t)clamp(mouse_x, 0.f, (float)app.devices.screen->width);
			app.devices.mouse.y = (uint16_t)clamp(mouse_y, 0.f, (float)app.devices.screen->height);
			buxn_mouse_set_button(
				&app.devices.mouse,
				0,
				event->type == SAPP_EVENTTYPE_TOUCHES_BEGAN
			);
			update_mouse = true;
		} break;
		case SAPP_EVENTTYPE_KEY_DOWN:
		case SAPP_EVENTTYPE_KEY_UP: {
			bool down = event->type == SAPP_EVENTTYPE_KEY_DOWN;
			int button = -1;
			char ch = 0;
			switch (event->key_code) {
				case SAPP_KEYCODE_LEFT_CONTROL:
				case SAPP_KEYCODE_RIGHT_CONTROL:
					button = BUXN_CONTROLLER_BTN_A;
					break;
				case SAPP_KEYCODE_LEFT_ALT:
				case SAPP_KEYCODE_RIGHT_ALT:
					button = BUXN_CONTROLLER_BTN_B;
					break;
				case SAPP_KEYCODE_LEFT_SHIFT:
				case SAPP_KEYCODE_RIGHT_SHIFT:
					button = BUXN_CONTROLLER_BTN_SELECT;
					break;
				case SAPP_KEYCODE_HOME:
					button = BUXN_CONTROLLER_BTN_START;
					break;
				case SAPP_KEYCODE_UP:
					button = BUXN_CONTROLLER_BTN_UP;
					break;
				case SAPP_KEYCODE_DOWN:
					button = BUXN_CONTROLLER_BTN_DOWN;
					break;
				case SAPP_KEYCODE_LEFT:
					button = BUXN_CONTROLLER_BTN_LEFT;
					break;
				case SAPP_KEYCODE_RIGHT:
					button = BUXN_CONTROLLER_BTN_RIGHT;
					break;
				case SAPP_KEYCODE_ENTER:
					ch = '\r';
					break;
				case SAPP_KEYCODE_ESCAPE:
					ch = 27;
					break;
				case SAPP_KEYCODE_BACKSPACE:
					ch = 8;
					break;
				case SAPP_KEYCODE_TAB:
					ch = '\t';
					break;
				case SAPP_KEYCODE_DELETE:
					ch = 127;
					break;
				default:
					break;
			}
			if (button >= 0) {
				buxn_controller_send_button(app.vm, &app.devices.controller, 0, button, down);
			}
			if (ch > 0 && down) {
				buxn_controller_send_char(app.vm, &app.devices.controller, ch);
			}
		} break;
		case SAPP_EVENTTYPE_CHAR: {
			uint32_t ch = event->char_code;
			if (ch <= 127) {
				// Sync the modifiers in case we missed their release due to
				// focus change
				buxn_controller_set_button(
					&app.devices.controller,
					0,
					BUXN_CONTROLLER_BTN_A,
					(event->modifiers & SAPP_MODIFIER_CTRL) != 0
				);
				buxn_controller_set_button(
					&app.devices.controller,
					0,
					BUXN_CONTROLLER_BTN_B,
					(event->modifiers & SAPP_MODIFIER_ALT) != 0
				);
				buxn_controller_set_button(
					&app.devices.controller,
					0,
					BUXN_CONTROLLER_BTN_SELECT,
					(event->modifiers & SAPP_MODIFIER_SHIFT) != 0
				);
				// Send the actual character
				buxn_controller_send_char(app.vm, &app.devices.controller, ch);
			}
		} break;
		default:
			break;
	}

	if (update_mouse) {
		buxn_mouse_update(app.vm);
		app.devices.mouse.scroll_x = app.devices.mouse.scroll_y = 0;
	}
}

sapp_desc
sokol_main(int argc, char* argv[]) {
	memset(&app, 0, sizeof(app));
	app.args = (args_t){
		.argc = argc,
		.argv = (const char**)argv,
	};
	platform_parse_args(&app.args);

	blog_init(&(blog_options_t){
		.current_filename = __FILE__,
		.current_depth_in_project = 1,
	});
	platform_init_log();
	platform_init_fs();

	return (sapp_desc){
		.init_cb = init,
		.frame_cb = frame,
		.event_cb = event,
		.cleanup_cb = cleanup,
		.width = 640,
		.height = 480,
		.sample_count = 1,
		.window_title = "buxn-gui",
		.icon.sokol_default = true,
		.logger.func = sokol_log,
	};
}
