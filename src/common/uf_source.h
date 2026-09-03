#ifndef UF_SOURCE_H
#define UF_SOURCE_H

#include "uf_common.h"

typedef struct {
    const char* file;
    uint32_t line;    /* 1-indexed */
    uint32_t col;     /* 1-indexed */
    uint32_t offset;  /* 0-indexed byte offset */
} SourceLoc;

typedef struct {
    SourceLoc start;
    SourceLoc end;
} SourceSpan;

static inline SourceLoc source_loc_make(const char* file, uint32_t line, uint32_t col, uint32_t offset) {
    SourceLoc loc;
    loc.file = file;
    loc.line = line;
    loc.col = col;
    loc.offset = offset;
    return loc;
}

static inline SourceSpan source_span_make(SourceLoc start, SourceLoc end) {
    SourceSpan span;
    span.start = start;
    span.end = end;
    return span;
}

static inline SourceSpan source_span_join(SourceSpan a, SourceSpan b) {
    SourceSpan span;
    span.start = (a.start.offset <= b.start.offset) ? a.start : b.start;
    span.end = (a.end.offset >= b.end.offset) ? a.end : b.end;
    return span;
}

#endif /* UF_SOURCE_H */
