#ifndef MEMORY_H__
#define MEMORY_H__

#include <stddef.h>	// size_t

#include "memory_provider.h"

extern MemoryProvider gp;
extern MemoryProvider hp;

int memory_init(void);

Memory memory_allocate(MemoryProvider , size_t );
int memory_free(MemoryProvider , Memory );

int memory_export(MemoryProvider , Memory , size_t );
int memory_close(int );

int memory_validate(Memory , size_t );
int memory_initialize(Memory , size_t );

const char *memory_get_error(void);

void memory_cleanup(void);

#endif
