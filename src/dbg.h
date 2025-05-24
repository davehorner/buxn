#ifndef BUXN_DBG_INTEGRATION_H
#define BUXN_DBG_INTEGRATION_H

#include <buxn/vm/vm.h>
#include <buxn/dbg/core.h>
#include <buxn/dbg/wire.h>
#include <buxn/dbg/transports/fd.h>

typedef struct {
	_Alignas(BUXN_DBG_ALIGNMENT) char dbg_mem[BUXN_DBG_SIZE];
	buxn_dbg_wire_t wire;
	buxn_dbg_transport_fd_t transport;
	void* dbg_in_mem;
	void* dbg_out_mem;
	buxn_dbg_t* dbg;
	buxn_vm_t* vm;
} buxn_dbg_integration_t;

void
buxn_dbg_integration_init(buxn_dbg_integration_t* dbg_int, buxn_vm_t* vm, int fd);

bool
buxn_dbg_integration_update(buxn_dbg_integration_t* dbg_int);

void
buxn_dbg_integration_cleanup(buxn_dbg_integration_t* dbg_int);

#endif
