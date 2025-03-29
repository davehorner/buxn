#ifndef BUXN_DEVICE_DATETIME_H
#define BUXN_DEVICE_DATETIME_H

#include <stdint.h>

struct buxn_vm_s;

uint8_t
buxn_datetime_dei(struct buxn_vm_s* vm, uint8_t addr);

#endif
