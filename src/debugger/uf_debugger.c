#include "uf_debugger.h"
#include "../runtime/uf_env.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- Tracer --- */

static void json_escape_print(FILE* out, const char* str) {
    if (!str) return;
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '\"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if ((unsigned char)*p < 32) {
                    fprintf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, out);
                }
                break;
        }
    }
}

static void tracer_hook(UfRuntime* rt, const UfDebugEvent* ev, void* user_ctx) {
    (void)rt;
    FILE* out = (FILE*)user_ctx;
    if (!out) out = stdout;

    switch (ev->type) {
        case UF_DEBUG_EVENT_STEP:
            fprintf(out, "{\"event\":\"step\",\"file\":\"");
            json_escape_print(out, ev->span.start.file ? ev->span.start.file : "<source>");
            fprintf(out, "\",\"line\":%u,\"col\":%u}\n", ev->span.start.line, ev->span.start.col);
            break;

        case UF_DEBUG_EVENT_CALL_ENTER:
            fprintf(out, "{\"event\":\"call_enter\",\"fn\":\"");
            json_escape_print(out, ev->fn_name ? ev->fn_name : "<anonymous>");
            fprintf(out, "\",\"file\":\"");
            json_escape_print(out, ev->span.start.file ? ev->span.start.file : "<source>");
            fprintf(out, "\",\"line\":%u}\n", ev->span.start.line);
            break;

        case UF_DEBUG_EVENT_CALL_EXIT: {
            char* val_str = uf_val_to_string(ev->val);
            fprintf(out, "{\"event\":\"call_exit\",\"fn\":\"");
            json_escape_print(out, ev->fn_name ? ev->fn_name : "<anonymous>");
            fprintf(out, "\",\"val\":\"");
            json_escape_print(out, val_str ? val_str : "null");
            fprintf(out, "\"}\n");
            free(val_str);
            break;
        }

        case UF_DEBUG_EVENT_VAR_BIND: {
            char* val_str = uf_val_to_string(ev->val);
            fprintf(out, "{\"event\":\"var_bind\",\"var\":\"");
            json_escape_print(out, ev->var_name ? ev->var_name : "<unknown>");
            fprintf(out, "\",\"val\":\"");
            json_escape_print(out, val_str ? val_str : "null");
            fprintf(out, "\",\"line\":%u}\n", ev->span.start.line);
            free(val_str);
            break;
        }

        case UF_DEBUG_EVENT_VAR_ASSIGN: {
            char* val_str = uf_val_to_string(ev->val);
            fprintf(out, "{\"event\":\"var_assign\",\"var\":\"");
            json_escape_print(out, ev->var_name ? ev->var_name : "<unknown>");
            fprintf(out, "\",\"val\":\"");
            json_escape_print(out, val_str ? val_str : "null");
            fprintf(out, "\",\"line\":%u}\n", ev->span.start.line);
            free(val_str);
            break;
        }

        case UF_DEBUG_EVENT_GC_START:
            fprintf(out, "{\"event\":\"gc_start\",\"bytes_allocated\":%zu}\n", ev->gc_bytes);
            break;

        case UF_DEBUG_EVENT_GC_END:
            fprintf(out, "{\"event\":\"gc_end\",\"bytes_allocated\":%zu}\n", ev->gc_bytes);
            break;

        case UF_DEBUG_EVENT_ERROR:
            fprintf(out, "{\"event\":\"error\",\"line\":%u}\n", ev->span.start.line);
            break;
    }
    fflush(out);
}

void uf_debugger_attach_tracer(UfRuntime* rt, FILE* out_stream) {
    if (!rt) return;
    rt->debug_hook = tracer_hook;
    rt->debug_user_ctx = out_stream ? out_stream : stdout;
}

/* --- Interactive Debugger --- */

typedef enum {
    DBG_MODE_CONTINUE,
    DBG_MODE_STEP_INTO,
    DBG_MODE_STEP_OVER
} DbgRunMode;

typedef struct {
    FILE* in;
    FILE* out;
    DbgRunMode mode;
    size_t step_over_frame_depth;
    int breakpoints[256];
    size_t bp_count;
} DbgState;

static bool is_breakpoint_hit(DbgState* state, int line) {
    for (size_t i = 0; i < state->bp_count; ++i) {
        if (state->breakpoints[i] == line) return true;
    }
    return false;
}

static void print_env_variables(UfRuntime* rt, FILE* out, UfEnv* env) {
    (void)rt;
    if (!env) return;
    fprintf(out, "--- Environment Variables ---\n");
    UfEnv* curr = env;
    while (curr) {
        if (curr->buckets) {
            for (size_t b = 0; b < curr->bucket_count; ++b) {
                UfEnvBinding* bind = curr->buckets[b];
                while (bind) {
                    char* s = uf_val_to_string(bind->value);
                    fprintf(out, "  %s = %s\n", bind->name, s ? s : "null");
                    free(s);
                    bind = bind->next;
                }
            }
        }
        curr = curr->parent;
    }
}

static void interactive_hook(UfRuntime* rt, const UfDebugEvent* ev, void* user_ctx) {
    DbgState* state = (DbgState*)user_ctx;
    if (!state) return;

    if (ev->type != UF_DEBUG_EVENT_STEP) return;

    int line = (int)ev->span.start.line;
    bool bp_hit = is_breakpoint_hit(state, line);
    bool should_pause = false;

    if (bp_hit) {
        fprintf(state->out, "[BREAKPOINT] Hit breakpoint at line %d (%s:%u:%u)\n",
                line, ev->span.start.file ? ev->span.start.file : "<source>",
                ev->span.start.line, ev->span.start.col);
        should_pause = true;
    } else if (state->mode == DBG_MODE_STEP_INTO) {
        should_pause = true;
    } else if (state->mode == DBG_MODE_STEP_OVER) {
        if (rt->frame_count <= state->step_over_frame_depth) {
            should_pause = true;
        }
    }

    if (!should_pause) return;

    fprintf(state->out, "--> %s:%u:%u\n",
            ev->span.start.file ? ev->span.start.file : "<source>",
            ev->span.start.line, ev->span.start.col);

    char line_buf[256];
    while (true) {
        fprintf(state->out, "(ufdb) ");
        fflush(state->out);

        if (!fgets(line_buf, sizeof(line_buf), state->in)) {
            /* EOF reached on input */
            state->mode = DBG_MODE_CONTINUE;
            break;
        }

        /* Strip trailing whitespace/newlines */
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r' || line_buf[len - 1] == ' ')) {
            line_buf[--len] = '\0';
        }

        char* cmd = line_buf;
        while (*cmd && isspace((unsigned char)*cmd)) cmd++;
        if (!*cmd) continue;

        if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
            state->mode = DBG_MODE_STEP_INTO;
            break;
        } else if (strcmp(cmd, "n") == 0 || strcmp(cmd, "next") == 0) {
            state->mode = DBG_MODE_STEP_OVER;
            state->step_over_frame_depth = rt->frame_count;
            break;
        } else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
            state->mode = DBG_MODE_CONTINUE;
            break;
        } else if (strncmp(cmd, "b ", 2) == 0 || strncmp(cmd, "break ", 6) == 0) {
            char* arg = strchr(cmd, ' ');
            while (arg && *arg && isspace((unsigned char)*arg)) arg++;
            if (arg) {
                if (strncmp(arg, "del ", 4) == 0) {
                    int del_line = atoi(arg + 4);
                    for (size_t i = 0; i < state->bp_count; ++i) {
                        if (state->breakpoints[i] == del_line) {
                            state->breakpoints[i] = state->breakpoints[--state->bp_count];
                            fprintf(state->out, "Breakpoint removed from line %d\n", del_line);
                            break;
                        }
                    }
                } else if (strcmp(arg, "list") == 0) {
                    fprintf(state->out, "Active breakpoints (%zu):\n", state->bp_count);
                    for (size_t i = 0; i < state->bp_count; ++i) {
                        fprintf(state->out, "  #%zu: line %d\n", i + 1, state->breakpoints[i]);
                    }
                } else {
                    int b_line = atoi(arg);
                    if (b_line > 0 && state->bp_count < 256) {
                        state->breakpoints[state->bp_count++] = b_line;
                        fprintf(state->out, "Breakpoint #%zu set at line %d\n", state->bp_count, b_line);
                    } else {
                        fprintf(state->out, "Invalid breakpoint line: %s\n", arg);
                    }
                }
            }
        } else if (strcmp(cmd, "breakpoints") == 0) {
            fprintf(state->out, "Active breakpoints (%zu):\n", state->bp_count);
            for (size_t i = 0; i < state->bp_count; ++i) {
                fprintf(state->out, "  #%zu: line %d\n", i + 1, state->breakpoints[i]);
            }
        } else if (strncmp(cmd, "p ", 2) == 0 || strncmp(cmd, "print ", 6) == 0) {
            char* arg = strchr(cmd, ' ');
            while (arg && *arg && isspace((unsigned char)*arg)) arg++;
            if (arg) {
                UfValue val;
                if (uf_env_lookup(rt->current_env, arg, &val)) {
                    char* s = uf_val_to_string(val);
                    fprintf(state->out, "%s = %s\n", arg, s ? s : "null");
                    free(s);
                } else {
                    fprintf(state->out, "Variable '%s' not found in current scope\n", arg);
                }
            }
        } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "bt") == 0) {
            fprintf(state->out, "Call stack (%zu frames):\n", rt->frame_count);
            for (size_t i = 0; i < rt->frame_count; ++i) {
                fprintf(state->out, "  #%zu  %s() at %s:%u\n",
                        i,
                        rt->frames[i].fn_name ? rt->frames[i].fn_name : "<anonymous>",
                        rt->frames[i].call_span.start.file ? rt->frames[i].call_span.start.file : "<unknown>",
                        rt->frames[i].call_span.start.line);
            }
        } else if (strcmp(cmd, "env") == 0) {
            print_env_variables(rt, state->out, rt->current_env);
        } else if (strcmp(cmd, "heap") == 0) {
            fprintf(state->out, "Heap: %zu bytes allocated, GC collections: %zu, threshold: %zu\n",
                    rt->bytes_allocated, rt->gc_count, rt->next_gc_threshold);
        } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            fprintf(state->out, "Quitting debugger...\n");
            rt->had_runtime_error = true;
            break;
        } else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            fprintf(state->out, "Unfish Debugger (ufdb) Commands:\n");
            fprintf(state->out, "  s, step              Step into statement\n");
            fprintf(state->out, "  n, next              Step over statement in current frame\n");
            fprintf(state->out, "  c, continue          Resume execution\n");
            fprintf(state->out, "  b <line>             Set breakpoint at line\n");
            fprintf(state->out, "  b del <line>         Delete breakpoint at line\n");
            fprintf(state->out, "  b list, breakpoints  List active breakpoints\n");
            fprintf(state->out, "  p <var>, print <var> Print variable value\n");
            fprintf(state->out, "  stack, bt            Show call stack backtrace\n");
            fprintf(state->out, "  env                  Inspect environment bindings\n");
            fprintf(state->out, "  heap                 Show GC and heap statistics\n");
            fprintf(state->out, "  q, quit              Quit execution\n");
            fprintf(state->out, "  h, help              Show this help message\n");
        } else {
            fprintf(state->out, "Unknown command '%s'. Type 'help' for instructions.\n", cmd);
        }
    }
}

void uf_debugger_attach_interactive(UfRuntime* rt, FILE* in_stream, FILE* out_stream) {
    if (!rt) return;
    DbgState* state = (DbgState*)malloc(sizeof(DbgState));
    if (!state) return;
    state->in = in_stream ? in_stream : stdin;
    state->out = out_stream ? out_stream : stdout;
    state->mode = DBG_MODE_STEP_INTO;
    state->step_over_frame_depth = 0;
    state->bp_count = 0;

    rt->debug_hook = interactive_hook;
    rt->debug_user_ctx = state;
}

void uf_debugger_detach(UfRuntime* rt) {
    if (!rt) return;
    if (rt->debug_hook == interactive_hook && rt->debug_user_ctx) {
        free(rt->debug_user_ctx);
    }
    rt->debug_hook = NULL;
    rt->debug_user_ctx = NULL;
}
