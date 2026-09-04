#ifndef UF_DIAGNOSTIC_H
#define UF_DIAGNOSTIC_H

#include "uf_common.h"
#include "uf_source.h"

typedef enum {
    UF_DIAG_LEX_ERROR,
    UF_DIAG_SYNTAX_ERROR,
    UF_DIAG_SEMANTIC_ERROR,
    UF_DIAG_RUNTIME_ERROR,
    UF_DIAG_WARNING,
    UF_DIAG_NOTE
} UfDiagKind;

typedef struct {
    UfDiagKind kind;
    SourceSpan span;
    const char* message;
    const char* hint;
} UfDiagnostic;

typedef void (*UfDiagCallback)(void* user_data, UfDiagKind kind, SourceSpan span, const char* message, const char* hint);

typedef struct {
    const char* source_text;
    const char* file_name;
    bool use_color;
    size_t error_count;
    size_t warning_count;
    UfDiagCallback callback;
    void* user_data;
} UfDiagnosticReporter;

void uf_diag_reporter_init(UfDiagnosticReporter* reporter, const char* file_name, const char* source_text);
void uf_report_diag(UfDiagnosticReporter* reporter, UfDiagKind kind, SourceSpan span, const char* message, const char* hint);
void uf_report_error(UfDiagnosticReporter* reporter, UfDiagKind kind, SourceSpan span, const char* fmt, ...);

#endif /* UF_DIAGNOSTIC_H */
