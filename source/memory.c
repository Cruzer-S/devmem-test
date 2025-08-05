#include "memory.h"

#include <stdlib.h>	// malloc(), free()
#include <string.h>	// strerror()
#include <errno.h>	// errno
#include <stdbool.h>	// false
#include <stdio.h>	// BUFSIZ, snprintf()

#include "amdgpu_memory.h"

#define SEED	10

#define ERROR(...) do {				\
	snprintf(error, BUFSIZ, __VA_ARGS__);	\
} while (false)

static struct memory_provider *provider = &amdgpu_memory_provider;
static char error[BUFSIZ];

Memory memory_allocate(size_t size)
{
	Memory memory;

	memory = provider->alloc(size);
	if (memory == NULL) {
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());
		return NULL;
	}

	return memory;
}

Memory memory_allocate_dmabuf(size_t size, int *dmabuf_fd)
{
	Memory memory;
	int ret;

	memory = provider->alloc(size);
	if (memory == NULL) {
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      provider->get_error());
		goto RETURN_NULL;
	}

	ret = amdgpu_memory_export_dmabuf(memory);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_export_dmabuf(): %s",
		      amdgpu_memory_get_error());
		goto FREE_MEMORY;
	}

	*dmabuf_fd = amdgpu_memory_get_dmabuf_fd(memory);

	return memory;

FREE_MEMORY:	(void) provider->free(memory);
RETURN_NULL:	return NULL;
}

int memory_free(Memory memory)
{
	int ret;

	ret = provider->free(memory);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      provider->get_error());
		return -1;
	}

	return 0;
}

int memory_free_dmabuf(Memory memory)
{
	int ret;

	ret = amdgpu_memory_close_dmabuf(memory);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_export_dmabuf(): %s",
		      amdgpu_memory_get_error());
		return -1;
	}

	ret = provider->free(memory);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      provider->get_error());
		return -1;
	}

	return 0;
}

int memory_initialize(Memory memory)
{
	char *buffer;
	size_t size;
	int ret;

	size = provider->get_size(memory);

	buffer = (char *) malloc(size);
	if (buffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	for (size_t i = 0; i < size; i++)
		buffer[i] = i % SEED;

	ret = provider->memcpy_to(memory, buffer, 0, size);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->memcpy_to(): %s",
		      provider->get_error());
		goto FREE_BUFFER;
	}

	free(buffer);

	return 0;

FREE_BUFFER:	free(buffer);
RETURN_ERROR:	return -1;
}

int memory_validate(Memory memory)
{
	char *buffer;
	size_t size;

	size = provider->get_size(memory);

	buffer = (char *) malloc(size);
	if (buffer == NULL)
		return -1;

	if (provider->memcpy_from(buffer, memory, 0, size) == -1) {
		free(buffer); 
		return -1;
	}

	for (size_t i = 0; i < size; i++) {
		if (buffer[i] != i % SEED) {
			free(buffer);
			return -1;
		}
	}

	free(buffer);

	return 0;
}

const char *memory_get_error(void)
{
	return error;
}
