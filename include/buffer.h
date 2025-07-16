#ifndef BUFFER_H__
#define BUFFER_H__

#include <stdbool.h>
#include <stddef.h>

#define BUFFER_PATTERN "ABCDEFGHIJKLMNOPQRSTU-VWXYZ"

char *buffer_create(char *pattern, size_t size);

bool buffer_validate(char *buffer, size_t size, char *pattern);

void buffer_destroy(char *buffer);

#endif
