#include <stdbool.h>		// bool, true, false
#include <stdlib.h>		// exit(), EXIT_FAILURE
#include <string.h>		// strerror()
#include <errno.h>		// errno

#include <arpa/inet.h>		// struct sockaddr_in

#include "logger.h"		// log()
#include "argument-parser.h"	// argument_parser...()
#include "memory_provider.h"

#include "client.h"
#include "socket.h"
#include "server.h"

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(*(ARR)))
#define ERROR(...) do {			\
	log(ERRN, __VA_ARGS__);		\
	exit(EXIT_FAILURE);		\
} while (true)
#define INFO(...) log(INFO, __VA_ARGS__)
#define WARN(...) log(WARN, __VA_ARGS__)

struct {
	char *bind_address;
	int bind_port;

	char *address;
	int port;

	int buffer_size;
	bool server;

	struct argument_info info[6];
} arguments = { .info = {
	{
		"bind-address", "a", "IP address to bind",
		(ArgumentValue *) &arguments.bind_address,
		ARGUMENT_PARSER_TYPE_STRING | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"bind-port", "p", "Port number to bind",
		(ArgumentValue *) &arguments.bind_port,
		ARGUMENT_PARSER_TYPE_INTEGER | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"buffer-size", "b", "Size of the VRAM buffer",
		(ArgumentValue *) &arguments.buffer_size,
		ARGUMENT_PARSER_TYPE_INTEGER | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"server", "s", "run as server (if not set, run as client)",
		(ArgumentValue *) &arguments.server,
		ARGUMENT_PARSER_TYPE_FLAG
	},
	{
		"address", "A", "IP address to connect",
		(ArgumentValue *) &arguments.address,
		ARGUMENT_PARSER_TYPE_STRING
	},
	{
		"port", "P", "Port number to connect",
		(ArgumentValue *) &arguments.port,
		ARGUMENT_PARSER_TYPE_INTEGER
	}
}};

static void parse_argument(ArgumentParser parser, int argc, char *argv[])
{
	for (int i = 0; i  < ARRAY_SIZE(arguments.info); i++)
		argument_parser_add(parser, arguments.info + i);

	if (argument_parser_parse(parser) == -1)
		ERROR("failed to argument_parser_parse(): %s",
      		    argument_parser_get_error(parser));

	INFO("bind-address: %s", arguments.bind_address);
	INFO("bind-port: %d", arguments.bind_port);

	INFO("buffer_size: %d", arguments.buffer_size);
	INFO("server: %s", arguments.server ? "Server" : "Client");

	if (!arguments.server) {
		INFO("connect-address: %s", arguments.address);
		INFO("connect-port: %d", arguments.port);
	}
}

static void do_server(int sockfd, Memory context)
{
	Server server;

	INFO("setup server");
	server = server_setup(sockfd, context);
	if (server == NULL)
		ERROR("failed to server_setup(): %s", server_get_error());

	INFO("start server");
	if (server_run_as_tcp(server) == -1)
		ERROR("failed to server_run_as_tcp(): %s", server_get_error());

	INFO("cleanup server");
	server_cleanup(server);
}

static void do_client(int sockfd, Memory context)
{
	Client client;
	struct sockaddr_in sockaddr;
	socklen_t addrlen;

	INFO("setup client");
	client = client_setup(sockfd, context);
	if (client == NULL)
		ERROR("failed to client_setup(): %s", client_get_error());

	memset(&sockaddr, 0x00, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = inet_addr(arguments.address);
	sockaddr.sin_port = htons(arguments.port);

	INFO("start client");
	if (client_run_as_tcp(client,
		       	      (struct sockaddr *) &sockaddr,
		       	      sizeof(struct sockaddr_in)) == -1)
		ERROR("failed to client_run_as_tcp(): %s", client_get_error());

	INFO("cleanup client");
	client_cleanup(client);
}

int main(int argc, char *argv[])
{
	struct memory_provider *provider;
	ArgumentParser parser;

	Memory context;
	int sockfd;

	if ( !logger_initialize() ) {
		fprintf(stderr, "failed to logger_initialize(): %s",
	  		strerror(errno));
		exit(EXIT_FAILURE);
	}

	INFO("create argument parser");
	parser = argument_parser_create(argc, argv);
	if (parser == NULL)
		ERROR("failed to argument_parser_create()");

	parse_argument(parser, argc, argv);

	INFO("create socket: %s:%d",
      	     arguments.bind_address, arguments.bind_port);
	sockfd = socket_create(arguments.bind_address, arguments.bind_port);
	if (sockfd == -1)
		ERROR("failed to socket_create(): %s", socket_get_error());

	provider = &amdgpu_memory_provider;

	INFO("allocate GPU buffer: size %d", arguments.buffer_size);
	context = provider->alloc(arguments.buffer_size);
	if (context == NULL)
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());

	if (arguments.server) {
		do_server(sockfd, context);
	} else {
		do_client(sockfd, context);
	}

	INFO("free GPU buffer");
	if (provider->free(context) == -1)
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      provider->get_error());

	INFO("destroy socket");
	socket_destroy(sockfd);

	argument_parser_destroy(parser);

	logger_destroy();

	return 0;
}
