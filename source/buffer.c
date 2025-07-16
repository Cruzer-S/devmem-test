#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

char *buffer_create(char *pattern, size_t size)
{
	size_t len;
	char *buffer;

 	buffer = (char *) malloc(size);
	if (buffer == NULL)
		return NULL;

	len = strlen(pattern);
	for (int i = 0; i < size; i++)
		buffer[i] = pattern[i % len];

	return buffer;
}

bool buffer_validate(char *buffer, size_t size, char *pattern)
{
	size_t len = strlen(pattern);

	for (int i = 0; i < size; i++)
		if (buffer[i] != pattern[i % len]) {
			printf("invalid at %d\n", i);
			return false;
		}

	return true;
}

void buffer_destroy(char *buffer)
{
	free(buffer);
}
