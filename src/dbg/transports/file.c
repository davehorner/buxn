#define _GNU_SOURCE
#include <fcntl.h>

int
buxn_dbg_transport_file(const char* path) {
	return open(path, O_RDWR, 0);
}
