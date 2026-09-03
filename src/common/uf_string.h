#ifndef UF_STRING_H
#define UF_STRING_H

#include "uf_common.h"
#include "uf_arena.h"

/* Dynamic string buffer */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} UfStrBuf;

void uf_strbuf_init(UfStrBuf* buf);
void uf_strbuf_append_char(UfStrBuf* buf, char c);
void uf_strbuf_append(UfStrBuf* buf, const char* str);
void uf_strbuf_append_len(UfStrBuf* buf, const char* str, size_t len);
void uf_strbuf_reset(UfStrBuf* buf);
void uf_strbuf_free(UfStrBuf* buf);
char* uf_strbuf_detach(UfStrBuf* buf);

/* String Interning */
typedef struct UfInternEntry {
    const char* str;
    size_t length;
    uint32_t hash;
    struct UfInternEntry* next;
} UfInternEntry;

typedef struct {
    UfArena* arena;
    UfInternEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
} UfInterner;

void uf_interner_init(UfInterner* interner, UfArena* arena);
const char* uf_intern(UfInterner* interner, const char* str, size_t len);
const char* uf_intern_cstr(UfInterner* interner, const char* str);
void uf_interner_free(UfInterner* interner);

uint32_t uf_hash_bytes(const char* data, size_t len);

#endif /* UF_STRING_H */
