#include <stdbool.h>		// bool, true, false
#include <stdlib.h>		// exit(), EXIT_FAILURE
#include <string.h>		// strerror()
#include <errno.h>		// errno

#include <sys/time.h>

#include "logger.h"		// log()
#include "argument-parser.h"	// argument_parser...()
#include "memory_provider.h"

#include "client.h"
#include "server.h"

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(*(ARR)))
#define BYTES_TO_GBPS(BYTES, SECONDS)				\
	(((double)(BYTES) * 8.0) / ((double)(SECONDS) * 1e9))
#define GET_ELAPSED(START, END)					\
	(  ((END).tv_sec - (START).tv_sec)			\
	 + ((END).tv_usec - (START).tv_usec) * 1e-6 )
#define ERROR(...) do {			\
	log(ERRN, __VA_ARGS__);		\
	exit(EXIT_FAILURE);		\
} while (true)
#define INFO(...) log(INFO, __VA_ARGS__)
#define WARN(...) log(WARN, __VA_ARGS__)

#define SEED	10

struct {
	char *bind_address;
	int bind_port;

	char *address;
	int port;

	int buffer_size;
	bool server;

	bool do_validation;

	struct argument_info info[7];
} static arguments = { .info = {
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
		"server", "s", "Run as server (if not set, run as client)",
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
	},
	{
		"validate", "v", "Do memory validation",
		(ArgumentValue *) &arguments.do_validation,
		ARGUMENT_PARSER_TYPE_FLAG
	}
}};

static struct memory_provider *provider = &amdgpu_memory_provider;

static void initialize_memory(Memory memory)
{
	char *buffer;
	size_t size;
	int ret;

	size = provider->get_size(memory);

	buffer = malloc(size);
	if (buffer == NULL)
		ERROR("failed to malloc(): %s", strerror(errno));

	for (size_t i = 0; i < size; i++)
		buffer[i] = i % SEED;

	ret = provider->memcpy_to(memory, buffer, 0, size);
	if (ret == -1)
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());

	free(buffer);
}

static void validate_memory(Memory memory)
{
	char *buffer;
	size_t size;
	int count;

	size = provider->get_size(memory);
	buffer = malloc(size);
	if (buffer == NULL)
		ERROR("failed to malloc(): %s", strerror(errno));

	if (provider->memcpy_from(buffer, memory, 0, size) == -1)
		ERROR("failed to amdgpu_memory_provider->memcpy_from(): %s",
		      provider->get_error());

	count = 0;
	for (size_t i = 0; i < size; i++) {
		if (buffer[i] != i % SEED) {
			WARN("memory invalid at %zu: expected %d, but %d",
				i, i % SEED, buffer[i]
			);
			count++;
		}

		if (count >= 10)
			ERROR("failed to validate_memory()");
	}

	free(buffer);
}


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
	INFO("do_validation: %s", arguments.do_validation ? "true" : "false");

	if (!arguments.server) {
		INFO("connect-address: %s", arguments.address);
		INFO("connect-port: %d", arguments.port);
	}
}

static void do_server(Memory context, char *address, int port)
{
	Server server;
	struct timeval start, end;

	INFO("setup server");
	server = server_setup(context, address, port);
	if (server == NULL)
		ERROR("failed to server_setup(): %s", server_get_error());

	INFO("start server");

	gettimeofday(&start, NULL);
	for (int i = 0; i < 1024; i++) {
		if (server_run_as_tcp(server) == -1)
			ERROR("failed to server_run_as_tcp(): %s",
			      server_get_error());

		if (arguments.do_validation)
			validate_memory(context);
	}
	gettimeofday(&end, NULL);
	
	INFO("Elapsed time: %.6f seconds", GET_ELAPSED(start, end));
	INFO("Total recieved: %.lf", 1024.0 * arguments.buffer_size);
	INFO("Bandwidth: %.6f Gbps",
      	     BYTES_TO_GBPS(1024.0 * arguments.buffer_size,
	     GET_ELAPSED(start, end)));

	INFO("cleanup server");
	server_cleanup(server);
}

static void do_client(Memory context,
		      char *bind_addr, int bind_port,
		      char *address, int port)
{
	Client client;
	struct timeval start, end;

	INFO("setup client");
	client = client_setup(context, bind_addr, bind_port);
	if (client == NULL)
		ERROR("failed to client_setup(): %s", client_get_error());

	if (arguments.do_validation)
		initialize_memory(context);

	INFO("start client");

	gettimeofday(&start, NULL);
	for (int i = 0; i < 1024; i++) {
		if (client_run_as_tcp(client, address, port) == -1)
			ERROR("failed to client_run_as_tcp(): %s",
			      client_get_error());
	}
	gettimeofday(&end, NULL);
	INFO("Elapsed time: %.6lf seconds", GET_ELAPSED(start, end));
	INFO("Total sent: %.0lf", 1024.0 * arguments.buffer_size);
	INFO("Bandwidth: %.6lf Gbps",
      	     BYTES_TO_GBPS(1024.0 * arguments.buffer_size,
	     GET_ELAPSED(start, end)));

	INFO("cleanup client");
	client_cleanup(client);
}

int main(int argc, char *argv[])
{
	ArgumentParser parser;
	Memory context;

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

	INFO("allocate GPU buffer: size %d", arguments.buffer_size);
	context = provider->alloc(arguments.buffer_size);
	if (context == NULL)
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());
	
	if (arguments.server) {
		do_server(context,
	    		  arguments.bind_address, arguments.bind_port);
	} else {
		do_client(context,
	    		  arguments.bind_address, arguments.bind_port,
			  arguments.address, arguments.port);
	}

	INFO("free GPU buffer");
	if (provider->free(context) == -1)
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      provider->get_error());

	argument_parser_destroy(parser);

	logger_destroy();

	return 0;
}
