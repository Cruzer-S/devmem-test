#include <stdlib.h>
#include <time.h>

#include <unistd.h>

#include <net/if.h>

#include "logger.h"

#include "buffer.h"
#include "memory.h"
#include "socket.h"
#include "server.h"
#include "client.h"

#define NUM_PAGES	16000
#define BUFFER_SIZE	(4096 * NUM_PAGES)

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

void validate_data(void)
{
	char *buffer;
	char *result;

 	buffer = malloc(BUFFER_SIZE);
	if (buffer == NULL)
		ERR(PERRN, "failed to malloc(): ");

	amdgpu_dmabuf_provider.memcpy_from(
		buffer, membuf, 0, BUFFER_SIZE
	);

	if (buffer_validate(buffer, BUFFER_SIZE, BUFFER_PATTERN))
		result = "valid";
	else
		result = "invalid";

	log(INFO, "validate_data: %s", result);

	free(buffer);
}

int main(int argc, char *argv[])
{
	clock_t start, finish;

	char *address, *interface;
	bool is_dma, is_server;
	int queue_start, nqueue;
	int port, ifindex;
	
	int sockfd;

	logger_initialize();

	if (argc != 8)
		ERR(ERRN, "usage: %s <is_server> <address> <port> "
	  		            "<is_dma> <interface> "
      				    "<start-queue> <nqueue>", argv[0]);

	is_server = (bool) strtol(argv[1], NULL, 10);
	address = argv[2];
	port = strtol(argv[3], NULL, 10);

	is_dma = (bool) strtol(argv[4], NULL, 10);
	if (is_dma) {
		ifindex = if_nametoindex(argv[5]);
		if (ifindex == 0)
			ERR(PERRN, "failed to if_nametoindex(): ");

		queue_start = strtol(argv[6], NULL, 10);
		nqueue = strtol(argv[7], NULL, 10);
	} else {
		nqueue = ifindex = -1;
	}

	memory_setup(BUFFER_SIZE, ifindex, queue_start);
	socket_create(address, port, is_server);

	start = clock();
	if (is_server)	server_start(is_dma);
	else		client_start(is_dma, BUFFER_SIZE);
	finish = clock();

	socket_destroy();
	log(INFO, "time: %lf", (double)(finish - start) / CLOCKS_PER_SEC);

	if (is_server)
		validate_data();

	memory_cleanup();


	logger_destroy();

	return 0;
}
