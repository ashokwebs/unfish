#ifndef UF_ARRAY_H
#define UF_ARRAY_H

#include "uf_common.h"

#define UF_ARRAY(T) struct { T* data; size_t count; size_t capacity; }

#define uf_array_init(arr) do { \
    (arr)->data = NULL; \
    (arr)->count = 0; \
    (arr)->capacity = 0; \
} while(0)

#define uf_array_free(arr) do { \
    free((arr)->data); \
    (arr)->data = NULL; \
    (arr)->count = 0; \
    (arr)->capacity = 0; \
} while(0)

#define uf_array_push(arr, item) do { \
    if ((arr)->count + 1 > (arr)->capacity) { \
        size_t _new_cap = (arr)->capacity == 0 ? 8 : (arr)->capacity * 2; \
        void* _new_data = realloc((arr)->data, _new_cap * sizeof(*(arr)->data)); \
        if (!_new_data) { \
            fprintf(stderr, "Fatal error: Out of memory in dynamic array\n"); \
            abort(); \
        } \
        (arr)->data = _new_data; \
        (arr)->capacity = _new_cap; \
    } \
    (arr)->data[(arr)->count++] = (item); \
} while(0)

#endif /* UF_ARRAY_H */
