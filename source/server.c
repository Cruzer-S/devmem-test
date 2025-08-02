#include "server.h"

#include <stdio.h>	// BUFSIZ
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>	// strerror()
#include <errno.h>	// errno

#include <unistd.h>

#include <sys/socket.h>

#include "amdgpu_memory_provider.h"

struct server {
	int sockfd;
	struct amdgpu_membuf_buffer *buffer;
};

static char error[BUFSIZ];

Server server_setup(int sockfd, struct amdgpu_membuf_buffer *buffer)
{
	Server server;

	server = malloc(sizeof(struct server));
	if (server == NULL) {
		snprintf(error, BUFSIZ,
	   		 "failed to malloc(): %s", strerror(errno));
		return NULL;
	}

	server->sockfd = sockfd;
	server->buffer = buffer;

	return server;
}

int server_run_as_tcp(Server server)
{
	int clnt_fd;
	size_t recvlen;
	char *ubuffer;

	ubuffer = malloc(server->buffer->size);
	if (ubuffer == NULL)
		goto RETURN_ERROR;

	clnt_fd = accept(server->sockfd, NULL, 0);
	if (clnt_fd == -1)
		goto FREE_BUFFER;

	recvlen = 0;
	while (true) {
		int ret = recv(
			clnt_fd, ubuffer,
			server->buffer->size - recvlen, 0
		);
		if (ret == -1)
			goto CLOSE_CLNT_FD;

		if (ret == 0)
			break;

		amdgpu_membuf_provider.memcpy_to(
			server->buffer, ubuffer, recvlen, ret
		);

		recvlen += ret;
	}

	close(clnt_fd);
	free(ubuffer);

	return 0;

CLOSE_CLNT_FD:	close(clnt_fd);
FREE_BUFFER:	free(ubuffer);
RETURN_ERROR:	return -1;
}

void server_cleanup(Server server)
{
	free(server);
}

char *server_get_error(void)
{
	return error;
}
