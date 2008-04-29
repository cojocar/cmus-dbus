/* 
 * Copyright 2004-2005 Timo Hirvonen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "server.h"
#include "prog.h"
#include "command_mode.h"
#include "search_mode.h"
#include "options.h"
#include "xmalloc.h"
#include "player.h"
#include "file.h"
#include "compiler.h"
#include "debug.h"
#include "gbuf.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

int server_socket;
LIST_HEAD(client_head);

static union {
	struct sockaddr sa;
	struct sockaddr_un un;
	struct sockaddr_in in;
} addr;

#define MAX_CLIENTS 10

static const char *escape(const char *str)
{
	static char *buf = NULL;
	static size_t alloc = 0;
	size_t len = strlen(str);
	size_t need = len * 2 + 1;
	int s, d;

	if (need > alloc) {
		alloc = (need + 16) & ~(16 - 1);
		buf = xrealloc(buf, alloc);
	}

	d = 0;
	for (s = 0; str[s]; s++) {
		if (str[s] == '\\') {
			buf[d++] = '\\';
			buf[d++] = '\\';
			continue;
		}
		if (str[s] == '\n') {
			buf[d++] = '\\';
			buf[d++] = 'n';
			continue;
		}
		buf[d++] = str[s];
	}
	buf[d] = 0;
	return buf;
}

static int cmd_status(struct client *client)
{
	const char *status[] = { "stopped", "playing", "paused" };
	const struct track_info *ti;
	GBUF(buf);
	int i, ret;

	player_info_lock();
	gbuf_addf(&buf, "status %s\n", status[player_info.status]);
	ti = player_info.ti;
	if (ti) {
		gbuf_addf(&buf, "file %s\n", escape(ti->filename));
		gbuf_addf(&buf, "duration %d\n", ti->duration);
		gbuf_addf(&buf, "position %d\n", player_info.pos);
		for (i = 0; ti->comments[i].key; i++)
			gbuf_addf(&buf, "tag %s %s\n",
					ti->comments[i].key,
					escape(ti->comments[i].val));
	}
	gbuf_add_str(&buf, "\n");
	player_info_unlock();

	ret = write_all(client->fd, buf.buffer, buf.len);
	gbuf_free(&buf);
	return ret;
}

static void read_commands(struct client *client)
{
	char buf[1024];
	int pos = 0;
	int authenticated = addr.sa.sa_family == AF_UNIX;

	while (1) {
		int rc, s, i;

		rc = read(client->fd, buf + pos, sizeof(buf) - pos);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				return;
			goto close;
		}
		if (rc == 0)
			goto close;
		pos += rc;

		s = 0;
		for (i = 0; i < pos; i++) {
			const char *line;
			char *cmd, *arg;
			int ret;

			if (buf[i] != '\n')
				continue;

			buf[i] = 0;
			line = buf + s;
			s = i + 1;

			if (!authenticated) {
				if (!server_password) {
					d_print("password is unset, tcp/ip disabled\n");
					goto close;
				}
				authenticated = !strcmp(line, server_password);
				if (!authenticated) {
					d_print("authentication failed\n");
					goto close;
				}
				continue;
			}

			while (isspace(*line))
				line++;

			if (*line == '/') {
				int restricted = 0;
				line++;
				search_direction = SEARCH_FORWARD;
				if (*line == '/') {
					line++;
					restricted = 1;
				}
				search_text(line, restricted);
				ret = write_all(client->fd, "\n", 1);
			} else if (*line == '?') {
				int restricted = 0;
				line++;
				search_direction = SEARCH_BACKWARD;
				if (*line == '?') {
					line++;
					restricted = 1;
				}
				search_text(line, restricted);
				ret = write_all(client->fd, "\n", 1);
			} else if (parse_command(line, &cmd, &arg)) {
				if (!strcmp(cmd, "status")) {
					ret = cmd_status(client);
				} else {
					run_parsed_command(cmd, arg);
					ret = write_all(client->fd, "\n", 1);
				}
				free(cmd);
				free(arg);
			} else {
				// don't hang cmus-remote
				ret = write_all(client->fd, "\n", 1);
			}
			if (ret < 0) {
				d_print("write: %s\n", strerror(errno));
				goto close;
			}
		}
		memmove(buf, buf + s, pos - s);
		pos -= s;
	}
	return;
close:
	close(client->fd);
	list_del(&client->node);
	free(client);
}

void server_accept(void)
{
	struct client *client;
	struct sockaddr saddr;
	socklen_t saddr_size = sizeof(saddr);
	int fd;

	fd = accept(server_socket, &saddr, &saddr_size);
	if (fd == -1)
		return;

	fcntl(fd, F_SETFL, O_NONBLOCK);

	client = xnew(struct client, 1);
	client->fd = fd;
	list_add_tail(&client->node, &client_head);
}

void server_serve(struct client *client)
{
	/* unix connection is secure, other insecure */
	run_only_safe_commands = addr.sa.sa_family != AF_UNIX;
	read_commands(client);
	run_only_safe_commands = 0;
}

static void gethostbyname_failed(void)
{
	const char *error = "Unknown error.";

	switch (h_errno) {
	case HOST_NOT_FOUND:
	case NO_ADDRESS:
		error = "Host not found.";
		break;
	case NO_RECOVERY:
		error = "A non-recoverable name server error.";
		break;
	case TRY_AGAIN:
		error = "A temporary error occurred on an authoritative name server.";
		break;
	}
	die("gethostbyname: %s\n", error);
}

void server_init(char *address)
{
	int port = DEFAULT_PORT;
	int addrlen;

	if (strchr(address, '/')) {
		addr.sa.sa_family = AF_UNIX;
		strncpy(addr.un.sun_path, address, sizeof(addr.un.sun_path) - 1);

		addrlen = sizeof(struct sockaddr_un);
	} else {
		char *s = strchr(address, ':');
		struct hostent *hent;

		if (s) {
			*s++ = 0;
			port = atoi(s);
		}
		hent = gethostbyname(address);
		if (!hent)
			gethostbyname_failed();

		addr.sa.sa_family = hent->h_addrtype;
		switch (addr.sa.sa_family) {
		case AF_INET:
			memcpy(&addr.in.sin_addr, hent->h_addr_list[0], hent->h_length);
			addr.in.sin_port = htons(port);

			addrlen = sizeof(addr.in);
			break;
		default:
			die("unsupported address type\n");
		}
	}

	server_socket = socket(addr.sa.sa_family, SOCK_STREAM, 0);
	if (server_socket == -1)
		die_errno("socket");

	if (bind(server_socket, &addr.sa, addrlen) == -1) {
		int sock;

		if (errno != EADDRINUSE)
			die_errno("bind");

		/* address already in use */
		if (addr.sa.sa_family != AF_UNIX)
			die("cmus is already listening on %s:%d\n", address, port);

		/* try to connect to server */
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sock == -1)
			die_errno("socket");

		if (connect(sock, &addr.sa, addrlen) == -1) {
			if (errno != ENOENT && errno != ECONNREFUSED)
				die_errno("connect");

			/* server not running => dead socket */

			/* try to remove dead socket */
			if (unlink(addr.un.sun_path) == -1 && errno != ENOENT)
				die_errno("unlink");
			if (bind(server_socket, &addr.sa, addrlen) == -1)
				die_errno("bind");
		} else {
			/* server already running */
			die("cmus is already listening on socket %s\n", address);
		}
		close(sock);
	}

	if (listen(server_socket, MAX_CLIENTS) == -1)
		die_errno("listen");
}

void server_exit(void)
{
	close(server_socket);
	if (addr.sa.sa_family == AF_UNIX)
		unlink(addr.un.sun_path);
}
