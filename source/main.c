#include <stdlib.h>

#include <unistd.h>

#include <net/if.h>

#include "logger.h"

#include "memory.h"
#include "socket.h"
#include "server.h"
#include "client.h"
#include "argument-parser.h"

#define PAGE_SIZE	4096
#define NUM_PAGES	16000
#define BUFFER_SIZE	(PAGE_SIZE * NUM_PAGES)

#define ERR(...) do {		\
	log(PERRN, __VA_ARGS__);\
	exit(EXIT_FAILURE);	\
} while (true)

#define DEFINE_ARGUMENT(PARSER, VALUE, NAME, T)			\
	argument_parser_add(PARSER, NAME, NAME, NAME, 		\
		     	    VALUE, ARGUMENT_PARSER_TYPE_##T)

ArgumentValue interface;
ArgumentValue serv_addr, serv_port;
ArgumentValue clnt_addr, clnt_port;
ArgumentValue enable_dma, server;
ArgumentValue queue_start, num_queue;

void parse_argument(ArgumentParser parser)
{
	DEFINE_ARGUMENT(parser, &interface, "interface", STRING);
	DEFINE_ARGUMENT(parser, &serv_addr, "serv-addr", STRING);
	DEFINE_ARGUMENT(parser, &serv_port, "serv-port", INTEGER);
	DEFINE_ARGUMENT(parser, &clnt_addr, "clnt-addr", STRING);
	DEFINE_ARGUMENT(parser, &clnt_port, "clnt-port", INTEGER);
	DEFINE_ARGUMENT(parser, &enable_dma, "enable-dma", BOOLEAN);
	DEFINE_ARGUMENT(parser, &server, "server", BOOLEAN);
	DEFINE_ARGUMENT(parser, &queue_start, "queue-start", INTEGER);
	DEFINE_ARGUMENT(parser, &num_queue, "num-queue", INTEGER);

	server.b = false;
	enable_dma.b = false;

	if (argument_parser_parse(parser) == -1)
		ERR("failed to argument_parser_parse(): ");
}

void dump_args(void)
{
	log(INFO, "interface: %s", interface.s);
	log(INFO, "serv-addr: %s", serv_addr.s);
	log(INFO, "serv-port: %d", serv_port.i);
	log(INFO, "clnt-addr: %s", clnt_addr.s);
	log(INFO, "clnt-port: %d", clnt_port.i);
	log(INFO, "server: %s", server.b ? "server" : "client");
	log(INFO, "enable-dma: %s", enable_dma.b ? "true" : "false");
	log(INFO, "queue-start: %d", queue_start.i);
	log(INFO, "num-queue: %d", num_queue.i);
}

int main(int argc, char *argv[])
{
	ArgumentParser parser;
	int ifindex;

	logger_initialize();

	parser = argument_parser_create(argv);
	if (parser == NULL)
		ERR("failed to argument_parser_create(): ");

	parse_argument(parser);

	ifindex = if_nametoindex(interface.s);
	if (!enable_dma.b) {
		ifindex = 0;
		queue_start.i = 0;
	}

	memory_setup(BUFFER_SIZE, ifindex, queue_start.i, server.b);
	if (server.b)
		socket_create(serv_addr.s, serv_port.i, server.b);
	else
		socket_create(clnt_addr.s, clnt_port.i, server.b);

	if (server.b)	server_start(BUFFER_SIZE, enable_dma.b);
	else		client_start(serv_addr.s, serv_port.i, enable_dma.b, BUFFER_SIZE, interface.s);

	socket_destroy();

	if (server.b) {
		if (memory_validate(BUFFER_SIZE)) {
			log(INFO, "memory_validate(): true");
		} else {
			log(WARN, "memory_validate(): false");
		}
	}

	memory_cleanup();

	argument_parser_destroy(parser);

	logger_destroy();

	return 0;
}
