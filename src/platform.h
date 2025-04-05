#ifndef BUXN_PLATFORM_H
#define BUXN_PLATFORM_H

#include <stdint.h>

void
platform_init_log(void);

void
platform_init_fs(int argc, const char* argv[]);

void
platform_resize_window(uint16_t width, uint16_t height);

#endif
