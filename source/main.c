#include <stdbool.h>	// bool, true, false
#include <stdlib.h>	// exit(), EXIT_FAILURE
#include <string.h>	// strerror()
#include <errno.h>	// errno

#include <net/if.h>	// if_nametoindex()

#include "logger.h"
#include "argument-parser.h"

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(*(ARR)))
#define ERROR(...) do {			\
	log(PERRN, __VA_ARGS__);	\
	exit(EXIT_FAILURE);		\
} while (true)
#define INFO(...) log(INFO, __VA_ARGS__)
#define WARN(...) log(WARN, __VA_ARGS__)

struct argument_info arguments[] = {
	{ 	
		"interface", "i", "network interface",
		NULL, ARGUMENT_PARSER_TYPE_STRING
	}
};

static void parse_argument(int argc, char *argv[],
			   int *ifindex)
{
	ArgumentParser parser;
	char *interface;

	parser = argument_parser_create(argc, argv);
	if (parser == NULL)
		ERROR("failed to argument_parser_create(): ");

	arguments[0].output = (ArgumentValue *) &interface;

	for (int i = 0; i  < ARRAY_SIZE(arguments); i++)
		argument_parser_add(parser, &arguments[i]);

	if (argument_parser_parse(parser) == -1)
		ERROR("failed to argument_parser_parse(): %s: ",
      		    argument_parser_get_error(parser));

	*ifindex = if_nametoindex(interface);
	if (*ifindex == 0)
		ERROR("failed to if_nametoindex(): ");

	argument_parser_destroy(parser);
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

	parse_argument(argc, argv, &ifindex);

	logger_destroy();

	return 0;
}
