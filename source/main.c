#include <stdlib.h>

#include <unistd.h>

#include <net/if.h>

#include "logger.h"

#include "memory.h"
#include "socket.h"
#include "server.h"
#include "client.h"

#define NUM_PAGES	2048
#define BUFFER_SIZE	(getpagesize() * NUM_PAGES)

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

int main(int argc, char *argv[])
{
	char *address, *interface;
	bool is_dma, is_server;
	int nqueue, port, ifindex;
	
	int sockfd;

	logger_initialize();

	if (argc != 7)
		ERR(ERRN, "usage: %s <is_server> <address> <port> "
	  		            "<is_dma> <nqueue> <interface>", argv[0]);

	is_server = (bool) strtol(argv[1], NULL, 10);
	address = argv[2];
	port = strtol(argv[3], NULL, 10);

	is_dma = (bool) strtol(argv[4], NULL, 10);
	if (is_dma) {
		nqueue = strtol(argv[5], NULL, 10);

		ifindex = if_nametoindex(argv[6]);
		if (ifindex == 0)
			ERR(PERRN, "failed to if_nametoindex(): ");
	} else {
		nqueue = ifindex = -1;
	}

	socket_create(address, port, is_server);
	memory_setup(BUFFER_SIZE, ifindex, nqueue);

	if (is_server)	server_start(is_dma);
	else		client_start(is_dma, BUFFER_SIZE);

	memory_cleanup();
	socket_destroy();

	logger_destroy();

	return 0;
}
