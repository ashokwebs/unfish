#include "uf_arena.h"

#define DEFAULT_ARENA_CHUNK_SIZE (64 * 1024) /* 64 KB */
#define ARENA_ALIGNMENT 8

static inline size_t align_up(size_t n, size_t alignment) {
    return (n + (alignment - 1)) & ~(alignment - 1);
}

static UfArenaChunk* new_chunk(size_t capacity) {
    UfArenaChunk* chunk = (UfArenaChunk*)malloc(sizeof(UfArenaChunk) + capacity);
    if (!chunk) {
        fprintf(stderr, "Fatal error: Out of memory in arena allocator\n");
        abort();
    }
    chunk->next = NULL;
    chunk->capacity = capacity;
    chunk->used = 0;
    return chunk;
}

void uf_arena_init(UfArena* arena, size_t default_chunk_size) {
    if (default_chunk_size == 0) {
        default_chunk_size = DEFAULT_ARENA_CHUNK_SIZE;
    }
    arena->default_chunk_size = default_chunk_size;
    arena->current = new_chunk(default_chunk_size);
    arena->total_allocated = sizeof(UfArenaChunk) + default_chunk_size;
}

void* uf_arena_alloc(UfArena* arena, size_t size) {
    size_t aligned_size = align_up(size, ARENA_ALIGNMENT);

    if (arena->current->used + aligned_size > arena->current->capacity) {
        size_t next_capacity = arena->default_chunk_size;
        if (aligned_size > next_capacity) {
            next_capacity = aligned_size;
        }
        UfArenaChunk* chunk = new_chunk(next_capacity);
        chunk->next = arena->current;
        arena->current = chunk;
        arena->total_allocated += sizeof(UfArenaChunk) + next_capacity;
    }

    void* ptr = (void*)(arena->current->data + arena->current->used);
    arena->current->used += aligned_size;
    return ptr;
}

char* uf_arena_strndup(UfArena* arena, const char* str, size_t len) {
    char* copy = (char*)uf_arena_alloc(arena, len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

char* uf_arena_strdup(UfArena* arena, const char* str) {
    return uf_arena_strndup(arena, str, strlen(str));
}

void uf_arena_reset(UfArena* arena) {
    UfArenaChunk* chunk = arena->current;
    while (chunk) {
        chunk->used = 0;
        chunk = chunk->next;
    }
}

void uf_arena_free(UfArena* arena) {
    UfArenaChunk* chunk = arena->current;
    while (chunk) {
        UfArenaChunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
    arena->current = NULL;
    arena->total_allocated = 0;
}
