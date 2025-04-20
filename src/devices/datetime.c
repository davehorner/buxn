#include "datetime.h"
#include <time.h>
#include "../vm/vm.h"

// Copied from: https://git.sr.ht/~rabbits/uxn/tree/main/item/src/devices/datetime.c

uint8_t
buxn_datetime_dei(struct buxn_vm_s* vm, uint8_t addr) {
	time_t seconds = time(NULL);
	struct tm tm = { 0 };
	struct tm* t = localtime(&seconds);
	if(t == NULL) { t = &tm; }

	switch(addr) {
		case 0xc0: return (t->tm_year + 1900) >> 8;
		case 0xc1: return (t->tm_year + 1900);
		case 0xc2: return t->tm_mon;
		case 0xc3: return t->tm_mday;
		case 0xc4: return t->tm_hour;
		case 0xc5: return t->tm_min;
		case 0xc6: return t->tm_sec;
		case 0xc7: return t->tm_wday;
		case 0xc8: return t->tm_yday >> 8;
		case 0xc9: return t->tm_yday;
		case 0xca: return t->tm_isdst;
		default: return vm->device[addr];
	}
}
