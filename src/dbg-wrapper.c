#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <buxn/dbg/transports/from_str.h>
#include "bflag.h"

int
main(int argc, char* argv[]) {
	const char* transport = "abstract-listen:buxn/vm";
	int i;
	for (i = 1; i < argc; ++i) {
		const char* flag_value;
		const char* arg = argv[i];
		if ((flag_value = parse_flag(arg, "--help")) != NULL) {
			fprintf(
				stderr,
				"Usage: buxn-dbg-wrapper [flags] [--] <program> [args]\n"
				"Run a program with buxn debugger enabled\n"
				"\n"
				"Available flags:\n\n"
				"* --help: Print this message.\n"
				"* -transport=<transport>: How to wait for a debugger to connect.\n"
				"  Default value: abstract-listen:buxn/vm\n"
				"  Available transports:\n\n"
				"  * file:<path>: Open a file at path\n"
				"  * tcp-connect:<address>:<port>: Connect to an address\n"
				"  * tcp-listen:<port>: Listen on a port for a single connection\n"
				"  * unix-connect:<name>: Connect to a unix domain socket\n"
				"  * unix-listen:<name>: Listen on a unix domain socket for a single connection\n"
				"  * abstract-connect:<name>: Connect to an abstract socket\n"
				"  * abstract-listen:<name>: Listen on an abstract socket for a single connection\n"
			);
			return 0;
		} else if ((flag_value = parse_flag(arg, "-transport=")) != NULL) {
			transport = flag_value;
		} else if (strcmp(arg, "--") == 0) {
			i+= 1;
			break;
		} else {
			break;
		}
	}

	if (i >= argc) {
		fprintf(stderr, "A command is required\n");
		fprintf(stderr, "Usage: buxn-dbg-wrapper [flags] [--] <program> [args]\n");
		fprintf(stderr, "For more info: buxn-dbg-wrapper --help\n");
		return 1;
	}

	int debug_fd = buxn_dbg_transport_from_str(transport);
	if (debug_fd < 0) {
		fprintf(stderr, "Could not open transport %s: %s\n", transport, strerror(errno));
		return 1;
	}

	char buf[sizeof("2147483647")];
	snprintf(buf, sizeof(buf), "%d", debug_fd);
	setenv("BUXN_DBG_FD", buf, 1);

	signal(SIGPIPE, SIG_IGN);
	return execvp(argv[i], &argv[i]);
}
