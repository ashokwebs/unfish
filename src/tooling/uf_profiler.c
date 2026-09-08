#include "uf_profiler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void uf_profiler_init(UfProfiler* prof) {
    if (!prof) return;
    memset(prof, 0, sizeof(*prof));
    prof->enabled = true;
}

static size_t find_or_add_entry(UfProfiler* prof, const char* fn_name) {
    if (!fn_name || fn_name[0] == '\0') fn_name = "<anonymous>";
    for (size_t i = 0; i < prof->count; i++) {
        if (strcmp(prof->entries[i].name, fn_name) == 0) {
            return i;
        }
    }
    if (prof->count < UF_PROF_MAX_ENTRIES) {
        size_t idx = prof->count++;
        prof->entries[idx].name = fn_name;
        prof->entries[idx].call_count = 0;
        prof->entries[idx].total_time_ms = 0.0;
        prof->entries[idx].self_time_ms = 0.0;
        prof->entries[idx].max_time_ms = 0.0;
        return idx;
    }
    return 0;
}
static uint64_t profiler_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void uf_profiler_enter(UfProfiler* prof, const char* fn_name) {
    if (!prof || !prof->enabled) return;
    if (prof->call_stack_depth >= 1024) return;

    size_t entry_idx = find_or_add_entry(prof, fn_name);
    prof->entries[entry_idx].call_count++;

    size_t d = prof->call_stack_depth++;
    prof->call_start_ns[d] = profiler_now_ns();
    prof->entry_index_stack[d] = entry_idx;
    prof->callee_time_ms[d] = 0.0;
}

void uf_profiler_exit(UfProfiler* prof, const char* fn_name) {
    UF_UNUSED(fn_name);
    if (!prof || !prof->enabled || prof->call_stack_depth == 0) return;

    uint64_t end_ns = profiler_now_ns();

    size_t d = --prof->call_stack_depth;
    uint64_t start_ns = prof->call_start_ns[d];
    size_t entry_idx = prof->entry_index_stack[d];
    double callee_ms = prof->callee_time_ms[d];

    double elapsed_ms = (end_ns >= start_ns) ? (double)(end_ns - start_ns) / 1000000.0 : 0.0;

    double self_ms = elapsed_ms - callee_ms;
    if (self_ms < 0.0) self_ms = 0.0;

    prof->entries[entry_idx].total_time_ms += elapsed_ms;
    prof->entries[entry_idx].self_time_ms += self_ms;
    if (elapsed_ms > prof->entries[entry_idx].max_time_ms) {
        prof->entries[entry_idx].max_time_ms = elapsed_ms;
    }

    if (prof->call_stack_depth > 0) {
        prof->callee_time_ms[prof->call_stack_depth - 1] += elapsed_ms;
    }
}

static int compare_entries(const void* a, const void* b) {
    const UfProfileEntry* ea = (const UfProfileEntry*)a;
    const UfProfileEntry* eb = (const UfProfileEntry*)b;
    if (eb->total_time_ms > ea->total_time_ms) return 1;
    if (eb->total_time_ms < ea->total_time_ms) return -1;
    return 0;
}

void uf_profiler_report(UfProfiler* prof, FILE* out) {
    if (!prof || !out || prof->count == 0) return;

    /* Sort entries descending by total_time_ms */
    qsort(prof->entries, prof->count, sizeof(UfProfileEntry), compare_entries);

    fprintf(out, "\n=========================================================================================\n");
    fprintf(out, "                                UNFISH EXECUTION PROFILE                                 \n");
    fprintf(out, "=========================================================================================\n");
    fprintf(out, "%-28s | %10s | %15s | %14s | %13s\n", "Function Name", "Calls", "Total Time (ms)", "Self Time (ms)", "Max Time (ms)");
    fprintf(out, "-----------------------------+------------+-----------------+----------------+--------------\n");

    double grand_self = 0.0;
    uint64_t grand_calls = 0;
    for (size_t i = 0; i < prof->count; i++) {
        const UfProfileEntry* e = &prof->entries[i];
        fprintf(out, "%-28s | %10llu | %15.3f | %14.3f | %13.3f\n",
                e->name,
                (unsigned long long)e->call_count,
                e->total_time_ms,
                e->self_time_ms,
                e->max_time_ms);
        grand_self += e->self_time_ms;
        grand_calls += e->call_count;
    }
    fprintf(out, "-----------------------------+------------+-----------------+----------------+--------------\n");
    fprintf(out, "%-28s | %10llu | %15s | %14.3f | %13s\n", "TOTAL (accumulated self)", (unsigned long long)grand_calls, "-", grand_self, "-");
    fprintf(out, "=========================================================================================\n\n");
}
