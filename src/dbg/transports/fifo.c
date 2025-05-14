#define _GNU_SOURCE
#include <fcntl.h>

int
buxn_dbg_transport_fifo(const char* path) {
	return open(path, O_RDWR, 0);
}
