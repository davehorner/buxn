#define _GNU_SOURCE
#include "stream.h"
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

int
buxn_dbg_transport_stream_listen(struct sockaddr* addr, size_t socklen) {
	int fd = socket(addr->sa_family, SOCK_STREAM, 0);
	if (bind(fd, addr, socklen) != 0) {
		close(fd);
		return -1;
	}

	if (listen(fd, 1) != 0) {
		close(fd);
		return -1;
	}

	while (1) {
		int result = accept(fd, NULL, NULL);
		if (result < 0 && errno == EINTR) {
			continue;
		} else {
			close(fd);
			return result;
		}
	}
}

int
buxn_dbg_transport_stream_connect(struct sockaddr* addr, size_t socklen) {
	int fd = socket(addr->sa_family, SOCK_STREAM, 0);
	if (connect(fd, addr, socklen) == 0) {
		return fd;
	} else {
		close(fd);
		return -1;
	}
}

int
buxn_dbg_transport_unix_connect(const char* name) {
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	size_t name_len = strlen(name);
	if (name_len >= sizeof(addr.sun_path)) { return -1; }
	memcpy(addr.sun_path, name, name_len);
	addr.sun_path[name_len] = '\0';
	socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + name_len + 1;

	return buxn_dbg_transport_stream_connect((struct sockaddr*)&addr, socklen);
}

int
buxn_dbg_transport_unix_listen(const char* name) {
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	size_t name_len = strlen(name);
	if (name_len >= sizeof(addr.sun_path)) { return -1; }
	memcpy(addr.sun_path, name, name_len);
	addr.sun_path[name_len] = '\0';
	socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + name_len + 1;

	return buxn_dbg_transport_stream_listen((struct sockaddr*)&addr, socklen);
}

int
buxn_dbg_transport_abstract_connect(const char* name) {
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	size_t name_len = strlen(name);
	if (name_len >= sizeof(addr.sun_path)) { return -1; }
	memcpy(addr.sun_path + 1, name, name_len);
	addr.sun_path[0] = '\0';
	socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + name_len + 1;

	return buxn_dbg_transport_stream_connect((struct sockaddr*)&addr, socklen);
}

int
buxn_dbg_transport_abstract_listen(const char* name) {
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	size_t name_len = strlen(name);
	if (name_len >= sizeof(addr.sun_path)) { return -1; }
	memcpy(addr.sun_path + 1, name, name_len);
	addr.sun_path[0] = '\0';
	socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + name_len + 1;

	return buxn_dbg_transport_stream_listen((struct sockaddr*)&addr, socklen);
}
