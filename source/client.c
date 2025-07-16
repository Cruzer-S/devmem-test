#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include <sys/socket.h>

#include "buffer.h"
#include "socket.h"

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

void client_start(bool is_dma, size_t buffer_size)
{
	char *buffer;
	int retval;
	size_t sendlen;

	buffer = buffer_create(BUFFER_PATTERN, buffer_size);
	if (buffer == NULL)
		ERR(PERRN, "failed to buffer_create(): ");

	socket_connect();

	sendlen = 0;
	while (sendlen < buffer_size) {
		retval = send(
			sockfd, buffer + sendlen,
			buffer_size - sendlen, 0
		);

		if (retval == -1)
			ERR(PERRN, "failed to send(): ");

		sendlen += retval;
	}

	log(INFO, "sendlen: %zu", sendlen);
}
