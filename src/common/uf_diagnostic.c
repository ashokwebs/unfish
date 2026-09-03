#include "uf_diagnostic.h"
#include <stdarg.h>
#include <unistd.h>

void uf_diag_reporter_init(UfDiagnosticReporter* reporter, const char* file_name, const char* source_text) {
    reporter->file_name = file_name ? file_name : "<input>";
    reporter->source_text = source_text;
    reporter->use_color = isatty(fileno(stderr));
    reporter->error_count = 0;
    reporter->warning_count = 0;
}

static const char* diag_kind_str(UfDiagKind kind) {
    switch (kind) {
        case UF_DIAG_LEX_ERROR:      return "Lexical Error";
        case UF_DIAG_SYNTAX_ERROR:   return "Syntax Error";
        case UF_DIAG_SEMANTIC_ERROR: return "Semantic Error";
        case UF_DIAG_RUNTIME_ERROR:  return "Runtime Error";
        case UF_DIAG_WARNING:        return "Warning";
        case UF_DIAG_NOTE:           return "Note";
    }
    return "Error";
}

static const char* diag_color(UfDiagKind kind) {
    switch (kind) {
        case UF_DIAG_LEX_ERROR:
        case UF_DIAG_SYNTAX_ERROR:
        case UF_DIAG_SEMANTIC_ERROR:
        case UF_DIAG_RUNTIME_ERROR:
            return "\033[1;31m"; /* Bold Red */
        case UF_DIAG_WARNING:
            return "\033[1;33m"; /* Bold Yellow */
        case UF_DIAG_NOTE:
            return "\033[1;36m"; /* Bold Cyan */
    }
    return "\033[1;31m";
}

void uf_report_diag(UfDiagnosticReporter* reporter, UfDiagKind kind, SourceSpan span, const char* message, const char* hint) {
    if (kind == UF_DIAG_WARNING) {
        reporter->warning_count++;
    } else if (kind != UF_DIAG_NOTE) {
        reporter->error_count++;
    }

    const char* reset = reporter->use_color ? "\033[0m" : "";
    const char* bold  = reporter->use_color ? "\033[1m" : "";
    const char* cyan  = reporter->use_color ? "\033[1;34m" : "";
    const char* color = reporter->use_color ? diag_color(kind) : "";

    /* Header: Category: Message */
    fprintf(stderr, "%s%s%s: %s%s%s\n", color, diag_kind_str(kind), reset, bold, message, reset);

    /* Location: --> file:line:col */
    fprintf(stderr, "  %s-->%s %s:%u:%u\n", cyan, reset, span.start.file ? span.start.file : reporter->file_name, span.start.line, span.start.col);

    /* Extract the source line if source text is available */
    if (reporter->source_text && span.start.line > 0) {
        const char* p = reporter->source_text;
        uint32_t current_line = 1;
        while (*p && current_line < span.start.line) {
            if (*p == '\n') current_line++;
            p++;
        }

        if (current_line == span.start.line) {
            const char* line_start = p;
            while (*p && *p != '\n' && *p != '\r') {
                p++;
            }
            size_t line_len = p - line_start;

            /* Print line number gutter */
            fprintf(stderr, "   %s|%s\n", cyan, reset);
            fprintf(stderr, "%s%2u |%s %.*s\n", cyan, span.start.line, reset, (int)line_len, line_start);

            /* Print caret line */
            fprintf(stderr, "   %s|%s ", cyan, reset);
            uint32_t col = span.start.col > 0 ? span.start.col : 1;
            for (uint32_t i = 1; i < col; ++i) {
                fprintf(stderr, " ");
            }

            uint32_t span_len = 1;
            if (span.end.line == span.start.line && span.end.col > span.start.col) {
                span_len = span.end.col - span.start.col;
            }

            fprintf(stderr, "%s^", color);
            for (uint32_t i = 1; i < span_len; ++i) {
                fprintf(stderr, "~");
            }
            fprintf(stderr, "%s\n", reset);
            fprintf(stderr, "   %s|%s\n", cyan, reset);
        }
    }

    if (hint && strlen(hint) > 0) {
        fprintf(stderr, "%sHint:%s %s\n", bold, reset, hint);
    }
    fprintf(stderr, "\n");
}

void uf_report_error(UfDiagnosticReporter* reporter, UfDiagKind kind, SourceSpan span, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    uf_report_diag(reporter, kind, span, buffer, NULL);
}
