#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include <sys/socket.h>

#include "memory.h"
#include "socket.h"

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

size_t total;

void client_start(bool is_dma, size_t buffer_size)
{
	int retval;
	size_t sendlen;
	
	socket_connect();

	total = 0;

	for (int i = 0; i < 1024; i++) {
		sendlen = 0;
		while (sendlen < buffer_size) {
			retval = send(
				sockfd, buffer + sendlen,
				buffer_size - sendlen, 0
			);

			if (retval == -1)
				ERR(PERRN, "failed to send(): ");

			log(INFO, "send: %zu", retval);

			sendlen += retval;
		}

		total += sendlen;
	}

	log(INFO, "total: %zu", total);
}
