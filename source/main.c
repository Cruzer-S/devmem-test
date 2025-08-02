#include <stdbool.h>		// bool, true, false
#include <stdlib.h>		// exit(), EXIT_FAILURE
#include <string.h>		// strerror()
#include <errno.h>		// errno

#include "logger.h"		// log()
#include "argument-parser.h"	// argument_parser...()
#include "amdgpu_memory_provider.h"

#include "socket.h"
#include "server.h"

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(*(ARR)))
#define ERROR(...) do {			\
	log(PERRN, __VA_ARGS__);	\
	exit(EXIT_FAILURE);		\
} while (true)
#define INFO(...) log(INFO, __VA_ARGS__)
#define WARN(...) log(WARN, __VA_ARGS__)

struct {
	char *address;
	int port;
	int buffer_size;

	struct argument_info info[3];
} arguments = { .info = {
	{
		"a", "address", "IP address",
		(ArgumentValue *) &arguments.address,
		ARGUMENT_PARSER_TYPE_STRING | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"p", "port", "Port number",
		(ArgumentValue *) &arguments.port,
		ARGUMENT_PARSER_TYPE_INTEGER | ARGUMENT_PARSER_TYPE_MANDATORY
	},
	{
		"S", "buffer-size", "Size of the buffer",
		(ArgumentValue *) &arguments.buffer_size,
		ARGUMENT_PARSER_TYPE_INTEGER | ARGUMENT_PARSER_TYPE_MANDATORY
	}
}};

static void parse_argument(int argc, char *argv[])
{
	ArgumentParser parser;

	parser = argument_parser_create(argc, argv);
	if (parser == NULL)
		ERROR("failed to argument_parser_create(): ");

	for (int i = 0; i  < ARRAY_SIZE(arguments.info); i++)
		argument_parser_add(parser, arguments.info + i);

	if (argument_parser_parse(parser) == -1)
		ERROR("failed to argument_parser_parse(): %s: ",
      		    argument_parser_get_error(parser));

	INFO("address: %s", arguments.address);
	INFO("port: %d", arguments.port);
	INFO("buffer_size: %d", arguments.buffer_size);

	argument_parser_destroy(parser);
}

int main(int argc, char *argv[])
{
	ArgumentParser parser;
	Server server;

	struct amdgpu_memory_provider provider;
	amdgpu_memory_buffer buffer;
	int sockfd;

	provider = amdgpu_membuf_provider;

	if ( !logger_initialize() ) {
		fprintf(stderr, "failed to logger_initialize(): %s",
	  		strerror(errno));
		exit(EXIT_FAILURE);
	}

	parse_argument(argc, argv);

	sockfd = socket_create(arguments.address, arguments.port);
	if (sockfd == -1)
		ERROR("failed to socket_create(): %s", socket_get_error());

	buffer = provider.alloc(arguments.buffer_size);
	if (buffer == NULL)
		ERROR("failed to amdgpu_membuf_provider.alloc(%d)",
		      arguments.buffer_size);

	server = server_setup(sockfd, buffer);
	if (server == NULL)
		ERROR("failed to server_setup(): %s", server_get_error());

	provider.free(buffer);
	socket_destroy(sockfd);

	logger_destroy();

	return 0;
}
