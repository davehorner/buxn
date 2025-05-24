#ifndef BUXN_DBG_TRANSPORT_FD_H
#define BUXN_DBG_TRANSPORT_FD_H

#include <bserial.h>

struct buxn_dbg_s;
struct buxn_dbg_wire_s;

typedef struct {
	bserial_in_t in;
	bserial_out_t out;
	int fd;
} buxn_dbg_transport_fd_t;

void
buxn_dbg_transport_fd_init(buxn_dbg_transport_fd_t* transport, int fd);

void
buxn_dbg_transport_fd_wire(
	buxn_dbg_transport_fd_t* transport,
	struct buxn_dbg_wire_s* wire,
	bserial_ctx_config_t config,
	void* in_mem,
	void* out_mem
);

bool
buxn_dbg_transport_fd_update(buxn_dbg_transport_fd_t* transport, struct buxn_dbg_s* core);

#endif
