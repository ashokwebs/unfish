#ifndef UF_ARENA_H
#define UF_ARENA_H

#include "uf_common.h"

typedef struct UfArenaChunk {
    struct UfArenaChunk* next;
    size_t capacity;
    size_t used;
    char data[];
} UfArenaChunk;

typedef struct UfArena {
    UfArenaChunk* current;
    size_t default_chunk_size;
    size_t total_allocated;
} UfArena;

void uf_arena_init(UfArena* arena, size_t default_chunk_size);
void* uf_arena_alloc(UfArena* arena, size_t size);
char* uf_arena_strndup(UfArena* arena, const char* str, size_t len);
char* uf_arena_strdup(UfArena* arena, const char* str);
void uf_arena_reset(UfArena* arena);
void uf_arena_free(UfArena* arena);

#endif /* UF_ARENA_H */
