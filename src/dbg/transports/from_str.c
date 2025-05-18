#define _GNU_SOURCE
#include <buxn/dbg/transports/from_str.h>
#include <buxn/dbg/transports/file.h>
#include <buxn/dbg/transports/stream.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../../bflag.h"

int
buxn_dbg_transport_from_str(const char* str) {
	const char* arg;
	if        ((arg = parse_flag(str, "file:")) != NULL) {
		return buxn_dbg_transport_file(arg);
	} else if ((arg = parse_flag(str, "unix-connect:")) != NULL) {
		return buxn_dbg_transport_unix_connect(arg);
	} else if ((arg = parse_flag(str, "unix-listen:")) != NULL) {
		return buxn_dbg_transport_unix_listen(arg);
	} else if ((arg = parse_flag(str, "abstract-connect:")) != NULL) {
		return buxn_dbg_transport_abstract_connect(arg);
	} else if ((arg = parse_flag(str, "abstract-listen:")) != NULL) {
		return buxn_dbg_transport_abstract_listen(arg);
	} else if ((arg = parse_flag(str, "tcp-connect:")) != NULL) {
		char buf[sizeof("255.255.255.255:65535")];

		size_t len = strlen(arg);
		if (len > sizeof(buf)) { return -1; }
		memcpy(buf, arg, len);

		int i;
		for (i = 0; i < (int)len; ++i) {
			if (buf[i] == ':') {
				buf[i] = '\0';
				break;
			}
		}
		if (i >= (int)len) { return -1; }

		struct addrinfo hints = {
			.ai_family = AF_INET,
			.ai_socktype = SOCK_STREAM,
		};
		struct addrinfo* addrinfo;
		if (getaddrinfo(buf, buf + i + 1, &hints, &addrinfo) != 0) {
			return -1;
		}
		int fd = buxn_dbg_transport_stream_connect(addrinfo->ai_addr, addrinfo->ai_addrlen);
		freeaddrinfo(addrinfo);
		return fd;
	} else if ((arg = parse_flag(str, "tcp-listen:")) != NULL) {
		errno = 0;
		long port = strtol(arg, NULL, 10);
		if (errno == 0) {
			struct sockaddr_in addr = {
				.sin_family = AF_INET,
				.sin_addr.s_addr = htonl(INADDR_ANY),
				.sin_port = port,
			};
			return buxn_dbg_transport_stream_listen((struct sockaddr*)&addr, sizeof(addr));
		} else {
			return -1;
		}
	} else {
		return -1;
	}
}
