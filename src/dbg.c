#include "dbg.h"
#include <stdlib.h>

void
buxn_dbg_integration_init(buxn_dbg_integration_t* dbg_int, buxn_vm_t* vm, int fd) {
		buxn_dbg_transport_fd_init(&dbg_int->transport, fd);

		bserial_ctx_config_t config = buxn_dbg_protocol_recommended_bserial_config();
		dbg_int->dbg_in_mem = malloc(bserial_ctx_mem_size(config));
		dbg_int->dbg_out_mem = malloc(bserial_ctx_mem_size(config));
		buxn_dbg_transport_fd_wire(
			&dbg_int->transport,
			&dbg_int->wire, config,
			dbg_int->dbg_in_mem,
			dbg_int->dbg_out_mem
		);

		dbg_int->dbg = buxn_dbg_init(dbg_int->dbg_mem, &dbg_int->wire);
		vm->config.hook = buxn_dbg_vm_hook(dbg_int->dbg);
		dbg_int->vm = vm;

		buxn_dbg_request_pause(dbg_int->dbg);
}

bool
buxn_dbg_integration_update(buxn_dbg_integration_t* dbg_int) {
	if (dbg_int->vm == NULL) { return false; }

	buxn_dbg_transport_fd_update(&dbg_int->transport, dbg_int->dbg);
	if (buxn_dbg_should_hook(dbg_int->dbg)) {
		dbg_int->vm->config.hook = buxn_dbg_vm_hook(dbg_int->dbg);
		return true;
	} else {
		dbg_int->vm->config.hook = (buxn_vm_hook_t) { 0 };
		return false;
	}
}

void
buxn_dbg_integration_cleanup(buxn_dbg_integration_t* dbg_int) {
	free(dbg_int->dbg_in_mem);
	free(dbg_int->dbg_out_mem);
}
