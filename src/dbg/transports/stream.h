#ifndef BUXN_DBG_TRANSPORT_STREAM_H
#define BUXN_DBG_TRANSPORT_STREAM_H

#include <stdint.h>
#include <stddef.h>

struct sockaddr;

int
buxn_dbg_transport_unix_connect(const char* name);

int
buxn_dbg_transport_unix_listen(const char* name);

int
buxn_dbg_transport_abstract_connect(const char* name);

int
buxn_dbg_transport_abstract_listen(const char* name);

int
buxn_dbg_transport_stream_listen(struct sockaddr* addr, size_t socklen);

int
buxn_dbg_transport_stream_connect(struct sockaddr* addr, size_t socklen);

#endif
