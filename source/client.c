#include "client.h"

#include <stdio.h>
#include <string.h>
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

void client_tcp_start(size_t buffer_size)
{
	int retval;
	size_t sendlen;

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

void client_dma_start(size_t buffer_size, char *ifname)
{
	int retval;

	setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname) + 1);
}

void client_start(bool is_dma, size_t buffer_size, char *ifname)
{
	socket_connect();

	if (is_dma)	client_dma_start(buffer_size, ifname);
	else		client_tcp_start(buffer_size);
}
