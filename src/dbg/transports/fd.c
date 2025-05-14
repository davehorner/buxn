#include "fd.h"
#include "../core.h"
#include "../wire.h"
#include <poll.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>

#define BUXN_CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

static size_t
buxn_dbg_transport_fd_read(struct bserial_in_s* in, void* buf, size_t size) {
	buxn_dbg_transport_fd_t* transport = BUXN_CONTAINER_OF(in, buxn_dbg_transport_fd_t, in);
	ssize_t bytes_read = read(transport->fd, buf, size);
	return bytes_read > 0 ? bytes_read : 0;
}

static size_t
buxn_dbg_transport_fd_write(struct bserial_out_s* out, const void* buf, size_t size) {
	buxn_dbg_transport_fd_t* transport = BUXN_CONTAINER_OF(out, buxn_dbg_transport_fd_t, out);
	ssize_t bytes_written = write(transport->fd, buf, size);
	return bytes_written > 0 ? bytes_written : 0;
}

void
buxn_dbg_transport_fd_init(buxn_dbg_transport_fd_t* transport, int fd) {
	*transport = (buxn_dbg_transport_fd_t){
		.fd = fd,
		.in = { .read = buxn_dbg_transport_fd_read },
		.out = { .write = buxn_dbg_transport_fd_write },
	};
}

void
buxn_dbg_transport_fd_wire(
	buxn_dbg_transport_fd_t* transport,
	struct buxn_dbg_wire_s* wire,
	bserial_ctx_config_t config,
	void* in_mem,
	void* out_mem
) {
	wire->in = bserial_make_ctx(in_mem, config, &transport->in, NULL);
	wire->out = bserial_make_ctx(out_mem, config, NULL, &transport->out);
}

void
buxn_dbg_transport_fd_update(buxn_dbg_transport_fd_t* transport, struct buxn_dbg_s* core) {
	struct pollfd pfd = {
		.fd = transport->fd,
		.events = POLLIN,
	};

	if (poll(&pfd, 1, 0) > 0) {
		buxn_dbg_request_pause(core);
	}
}
