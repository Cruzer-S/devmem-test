#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include <sys/socket.h>

#include "socket.h"

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

static int send_all(int fd, char *buffer, size_t len)
{
	size_t sendlen = 0;
	int retval;

	while (sendlen < len) {
		retval = send(fd, buffer + sendlen, len - sendlen, 0);

		if (retval <= 0)
			return retval;

		sendlen += retval;
	}

	return sendlen;
}

void client_start(bool is_dma, size_t buffer_size)
{
	char buffer[BUFSIZ];
	int retval;
	size_t sendlen = 0;

	for (int i = 0; i < BUFSIZ; i++)
		buffer[i] = 'A' + (i % ('Z' - 'A'));

	socket_connect();

	while (sendlen < buffer_size) {
		size_t offset, len;

		retval = send_all(
			sockfd, buffer, (sendlen + BUFSIZ) < buffer_size ? 
			BUFSIZ : buffer_size - sendlen
		);

		if (retval == -1)
			ERR(PERRN, "failed to send(): ");

		sendlen += retval;
	}
}
