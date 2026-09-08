#ifndef UF_PROFILER_H
#define UF_PROFILER_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <time.h>
#include "../common/uf_common.h"

#define UF_PROF_MAX_ENTRIES 4096

typedef struct {
    const char* name;
    uint64_t call_count;
    double total_time_ms;
    double self_time_ms;   /* Excludes time spent in callees */
    double max_time_ms;    /* Slowest single call */
} UfProfileEntry;

typedef struct UfProfiler {
    UfProfileEntry entries[UF_PROF_MAX_ENTRIES];
    size_t count;
    uint64_t call_start_ns[1024];
    size_t entry_index_stack[1024];
    double callee_time_ms[1024];
    size_t call_stack_depth;
    bool enabled;
} UfProfiler;

void uf_profiler_init(UfProfiler* prof);
void uf_profiler_enter(UfProfiler* prof, const char* fn_name);
void uf_profiler_exit(UfProfiler* prof, const char* fn_name);
void uf_profiler_report(UfProfiler* prof, FILE* out);

#endif /* UF_PROFILER_H */
