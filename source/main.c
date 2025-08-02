#include <stdio.h>
#include <string.h>
#include <errno.h>
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

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(*(ARR)))
#define ERROR(...) do {			\
	log(PERRN, __VA_ARGS__);	\
	exit(EXIT_FAILURE);		\
} while (true)
#define INFO(...) log(INFO, __VA_ARGS__)
#define WARN(...) log(WARN, __VA_ARGS__)

char *interface;
char *serv_addr, *clnt_addr;
int serv_port, clnt_port;
bool enable_dma, server;
int queue_start, num_queue;

struct argument_info arguments[] = {
	{ 	"interface", "i", "network interface",
		(ArgumentValue *) &interface, ARGUMENT_PARSER_TYPE_STRING
	}, {	"serv-addr", "s", "server address",
		(ArgumentValue *) &serv_addr, ARGUMENT_PARSER_TYPE_STRING
	}, {	"clnt-addr", "c", "client address",
		(ArgumentValue *) &clnt_addr, ARGUMENT_PARSER_TYPE_STRING
	}, {	"serv-port", "P", "server port",
		(ArgumentValue *) &clnt_addr, ARGUMENT_PARSER_TYPE_INTEGER
	}, {	"clnt-port", "p", "client port",
		(ArgumentValue *) &clnt_port, ARGUMENT_PARSER_TYPE_INTEGER
	}, {	"enable-dma", "D", "enable dma",
		(ArgumentValue *) &enable_dma, ARGUMENT_PARSER_TYPE_FLAG
	}, {	"server", "S", "run as server",
		(ArgumentValue *) &server, ARGUMENT_PARSER_TYPE_FLAG
	}, {	"queue-start", "q", "start index of NIC queue",
		(ArgumentValue *) &queue_start, ARGUMENT_PARSER_TYPE_INTEGER
	}, {	"num-queue", "n", "number of NIC queue",
		(ArgumentValue *) &num_queue, ARGUMENT_PARSER_TYPE_INTEGER
	}
};

void parse_argument(ArgumentParser parser)
{
	for (int i = 0; i  < ARRAY_SIZE(arguments); i++)
		argument_parser_add(parser, &arguments[i]);

	if (argument_parser_parse(parser) == -1)
		ERROR("failed to argument_parser_parse(): %s",
      		    argument_parser_get_error(parser));
}

int main(int argc, char *argv[])
{
	ArgumentParser parser;
	int ifindex;

	if ( !logger_initialize() ) {
		fprintf(stderr, "failed to logger_initialize(): %s",
	  		strerror(errno));
		exit(EXIT_FAILURE);
	}

	parser = argument_parser_create(argc, argv);
	if (parser == NULL)
		ERROR("failed to argument_parser_create(): ");

	parse_argument(parser);

	ifindex = if_nametoindex(interface);
	if (!enable_dma) {
		ifindex = 0;
		queue_start = 0;
	}

	memory_setup(BUFFER_SIZE, ifindex, queue_start, server);
	if (server)
		socket_create(serv_addr, serv_port, server);
	else
		socket_create(clnt_addr, clnt_port, server);

	if (server)	server_start(BUFFER_SIZE, enable_dma);
	else		client_start(serv_addr, serv_port, enable_dma, BUFFER_SIZE, interface);

	socket_destroy();

	if (server) {
		if (memory_validate(BUFFER_SIZE)) {
			INFO("memory_validate(): true");
		} else {
			WARN("memory_validate(): false");
		}
	}

	memory_cleanup();

	argument_parser_destroy(parser);

	logger_destroy();

	return 0;
}
