#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <buxn/dbg/transports/from_str.h>

int
main(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(
			stderr,
			"Usage: buxn-dbg-wrapper <transport> <program> [args]\n"
			"Run a program with buxn debugger enabled\n"
			"\n"
			"`transport` can be one of:\n"
			"\n"
			"* fifo:<path>: Open a fifo file.\n"
			"* tcp-connect:<address>:<port>: Connect to an address.\n"
			"* tcp-listen:<port>: Listen on a port for a single connection.\n"
			"* unix-connect:<name>: Connect to a unix domain socket.\n"
			"* unix-listen:<name>: Listen on a unix domain socket for a single connection.\n"
			"* abstract-connect:<name>: Connect to an abstract socket.\n"
			"* abstract-listen:<name>: Listen on an abstract socket for a single connection.\n"
		);
		return 1;
	}

	int debug_fd = buxn_dbg_transport_from_str(argv[1]);
	if (debug_fd >= 0) {
		char buf[sizeof("2147483647")];
		snprintf(buf, sizeof(buf), "%d", debug_fd);
		setenv("BUXN_DEBUG_FD", buf, 1);
	}

	signal(SIGPIPE, SIG_IGN);
	return execvp(argv[2], &argv[2]);
}
