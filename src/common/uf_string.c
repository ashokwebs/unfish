#include "uf_string.h"

/* --- UfStrBuf --- */

void uf_strbuf_init(UfStrBuf* buf) {
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

static void ensure_strbuf_capacity(UfStrBuf* buf, size_t needed) {
    if (buf->length + needed + 1 > buf->capacity) {
        size_t new_cap = buf->capacity == 0 ? 32 : buf->capacity * 2;
        while (new_cap < buf->length + needed + 1) {
            new_cap *= 2;
        }
        buf->data = (char*)realloc(buf->data, new_cap);
        if (!buf->data) {
            fprintf(stderr, "Fatal error: Out of memory in string buffer\n");
            abort();
        }
        buf->capacity = new_cap;
    }
}

void uf_strbuf_append_char(UfStrBuf* buf, char c) {
    ensure_strbuf_capacity(buf, 1);
    buf->data[buf->length++] = c;
    buf->data[buf->length] = '\0';
}

void uf_strbuf_append_len(UfStrBuf* buf, const char* str, size_t len) {
    if (len == 0) return;
    ensure_strbuf_capacity(buf, len);
    memcpy(buf->data + buf->length, str, len);
    buf->length += len;
    buf->data[buf->length] = '\0';
}

void uf_strbuf_append(UfStrBuf* buf, const char* str) {
    if (str) {
        uf_strbuf_append_len(buf, str, strlen(str));
    }
}

void uf_strbuf_reset(UfStrBuf* buf) {
    buf->length = 0;
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

void uf_strbuf_free(UfStrBuf* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

char* uf_strbuf_detach(UfStrBuf* buf) {
    char* data = buf->data;
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
    return data ? data : strdup("");
}

/* --- UfInterner --- */

uint32_t uf_hash_bytes(const char* data, size_t len) {
    /* FNV-1a */
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

#define INTERN_INITIAL_BUCKETS 256

void uf_interner_init(UfInterner* interner, UfArena* arena) {
    interner->arena = arena;
    interner->bucket_count = INTERN_INITIAL_BUCKETS;
    interner->entry_count = 0;
    interner->buckets = (UfInternEntry**)calloc(interner->bucket_count, sizeof(UfInternEntry*));
    if (!interner->buckets) {
        fprintf(stderr, "Fatal error: Out of memory initializing interner\n");
        abort();
    }
}

static void interner_resize(UfInterner* interner) {
    size_t new_cap = interner->bucket_count * 2;
    UfInternEntry** new_buckets = (UfInternEntry**)calloc(new_cap, sizeof(UfInternEntry*));
    if (!new_buckets) return;

    for (size_t i = 0; i < interner->bucket_count; ++i) {
        UfInternEntry* entry = interner->buckets[i];
        while (entry) {
            UfInternEntry* next = entry->next;
            size_t idx = entry->hash & (new_cap - 1);
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }
    free(interner->buckets);
    interner->buckets = new_buckets;
    interner->bucket_count = new_cap;
}

const char* uf_intern(UfInterner* interner, const char* str, size_t len) {
    uint32_t hash = uf_hash_bytes(str, len);
    size_t idx = hash & (interner->bucket_count - 1);

    UfInternEntry* entry = interner->buckets[idx];
    while (entry) {
        if (entry->hash == hash && entry->length == len && memcmp(entry->str, str, len) == 0) {
            return entry->str;
        }
        entry = entry->next;
    }

    if (interner->entry_count + 1 > interner->bucket_count * 0.75) {
        interner_resize(interner);
        idx = hash & (interner->bucket_count - 1);
    }

    char* copy = uf_arena_strndup(interner->arena, str, len);
    UfInternEntry* new_entry = (UfInternEntry*)uf_arena_alloc(interner->arena, sizeof(UfInternEntry));
    new_entry->str = copy;
    new_entry->length = len;
    new_entry->hash = hash;
    new_entry->next = interner->buckets[idx];
    interner->buckets[idx] = new_entry;
    interner->entry_count++;

    return copy;
}

const char* uf_intern_cstr(UfInterner* interner, const char* str) {
    return uf_intern(interner, str, strlen(str));
}

void uf_interner_free(UfInterner* interner) {
    free(interner->buckets);
    interner->buckets = NULL;
    interner->bucket_count = 0;
    interner->entry_count = 0;
}
