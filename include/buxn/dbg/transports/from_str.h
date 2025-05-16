#ifndef BUXN_DBG_TRANSPORT_FROM_STR_H
#define BUXN_DBG_TRANSPORT_FROM_STR_H

/**
 * Make a transport fd from a string.
 *
 * Supported schemes:
 *
 * * fifo:<path>: Open a fifo file.
 * * tcp-connect:<address>:<port>: Connect to an address.
 * * tcp-listen:<port>: Listen on a port for a single connection.
 * * unix-connect:<name>: Connect to a unix domain socket.
 * * unix-listen:<name>: Listen on a unix domain socket for a single connection.
 * * abstract-connect:<name>: Connect to an abstract socket.
 * * abstract-listen:<name>: Listen on an abstract socket for a single connection
 */
int
buxn_dbg_transport_from_str(const char* str);

#endif
