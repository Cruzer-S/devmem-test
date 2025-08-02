#include <stdbool.h>		// bool, true, false
#include <stdlib.h>		// exit(), EXIT_FAILURE
#include <string.h>		// strerror()
#include <errno.h>		// errno

#include "logger.h"		// log()
#include "argument-parser.h"	// argument_parser...()
#include "memory_provider.h"

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
	char *address;
	int port;
	int buffer_size;
	bool server;

	struct argument_info info[4];
} arguments = { .info = {
	{
		"address", "a", "IP address",
		(ArgumentValue *) &arguments.address,
		ARGUMENT_PARSER_TYPE_STRING | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"port", "p", "Port number",
		(ArgumentValue *) &arguments.port,
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
	}
}};

static void parse_argument(int argc, char *argv[])
{
	ArgumentParser parser;

	parser = argument_parser_create(argc, argv);
	if (parser == NULL)
		ERROR("failed to argument_parser_create()");

	for (int i = 0; i  < ARRAY_SIZE(arguments.info); i++)
		argument_parser_add(parser, arguments.info + i);

	if (argument_parser_parse(parser) == -1)
		ERROR("failed to argument_parser_parse(): %s",
      		    argument_parser_get_error(parser));

	INFO("address: %s", arguments.address);
	INFO("port: %d", arguments.port);
	INFO("buffer_size: %d", arguments.buffer_size);
	INFO("server: %s", arguments.server ? "Server" : "Client");

	argument_parser_destroy(parser);
}

static void do_server(int sockfd, Memory context)
{
	Server server;

	server = server_setup(sockfd, context);
	if (server == NULL)
		ERROR("failed to server_setup(): %s", server_get_error());

	if (server_run_as_tcp(server) == -1)
		ERROR("failed to server_run_as_tcp(): %s", server_get_error());

	server_cleanup(server);
}

static void do_client(void)
{

}

int main(int argc, char *argv[])
{
	struct memory_provider *provider;

	Memory context;
	int sockfd;

	if ( !logger_initialize() ) {
		fprintf(stderr, "failed to logger_initialize(): %s",
	  		strerror(errno));
		exit(EXIT_FAILURE);
	}

	parse_argument(argc, argv);

	sockfd = socket_create(arguments.address, arguments.port);
	if (sockfd == -1)
		ERROR("failed to socket_create(): %s", socket_get_error());

	provider = &amdgpu_memory_provider;

	context = provider->alloc(arguments.buffer_size);
	if (context == NULL)
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());

	if (arguments.server) {
		do_server(sockfd, context);
	} else {
		do_client();
	}

	if (provider->free(context) == -1)
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      provider->get_error());

	socket_destroy(sockfd);

	logger_destroy();

	return 0;
}
