#ifndef UF_SEMANTIC_H
#define UF_SEMANTIC_H

#include "../common/uf_common.h"
#include "../common/uf_arena.h"
#include "../common/uf_diagnostic.h"
#include "../ast/uf_ast.h"

typedef enum {
    UF_SYM_VAR,
    UF_SYM_FUNCTION,
    UF_SYM_BUILTIN,
    UF_SYM_STRUCT
} UfSymbolKind;

typedef struct UfSymbol {
    const char* name;
    UfSymbolKind kind;
    SourceSpan span;
    int arity; /* For functions; -1 if variadic/unknown */
    const char* type_annotation; /* Annotated type name, or NULL */
    const char* return_type;     /* Annotated return type, or NULL */
    const char** param_types;    /* Annotated parameter types, or NULL */
    struct UfSymbol* next;
} UfSymbol;

typedef struct UfScope {
    struct UfScope* parent;
    bool is_function;
    UfSymbol** buckets;
    size_t bucket_count;
} UfScope;

typedef struct UfSemanticAnalyzer {
    UfScope* current_scope;
    UfScope* global_scope;
    int function_depth;
    int loop_depth;
    bool had_error;
    bool strict_mode;
    const char* current_fn_return_type;

    UfArena* arena;
    UfDiagnosticReporter* reporter;
} UfSemanticAnalyzer;

void uf_semantic_init(UfSemanticAnalyzer* analyzer,
                      UfArena* arena,
                      UfDiagnosticReporter* reporter);

void uf_semantic_add_symbol(UfSemanticAnalyzer* analyzer,
                            const char* name,
                            UfSymbolKind kind,
                            SourceSpan span,
                            int arity);

void uf_semantic_add_symbol_with_type(UfSemanticAnalyzer* analyzer,
                                      const char* name,
                                      UfSymbolKind kind,
                                      SourceSpan span,
                                      int arity,
                                      const char* type_annotation,
                                      const char* return_type,
                                      const char** param_types);

bool uf_analyze_program(UfSemanticAnalyzer* analyzer, UfProgram* program);
bool uf_typecheck_program(UfSemanticAnalyzer* analyzer, UfProgram* program);

#endif /* UF_SEMANTIC_H */
