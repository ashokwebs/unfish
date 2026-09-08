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
    UF_SYM_STRUCT,
    UF_SYM_ENUM,
    UF_SYM_ENUM_VARIANT,
    UF_SYM_TRAIT,
    UF_SYM_TYPE_PARAM
} UfSymbolKind;

typedef struct UfTraitInfo {
    const char* name;
    SourceSpan span;
    const char** method_names;
    size_t* method_param_counts;
    const char*** method_param_names;
    const char*** method_param_types;
    const char** method_return_types;
    size_t method_count;
    struct UfTraitInfo* next;
} UfTraitInfo;

typedef struct UfTraitImplInfo {
    const char* struct_name;
    const char* trait_name;
    struct UfTraitImplInfo* next;
} UfTraitImplInfo;

typedef struct UfSymbol {
    const char* name;
    UfSymbolKind kind;
    SourceSpan span;
    int arity; /* For functions; -1 if variadic/unknown */
    int min_arity; /* Minimum required arguments; -1 if unknown */
    bool has_rest;
    const char* type_annotation; /* Annotated type name, or NULL */
    const char* return_type;     /* Annotated return type, or NULL */
    const char** param_types;    /* Annotated parameter types, or NULL */
    const char* trait_bound;     /* For UF_SYM_TYPE_PARAM: bound trait name, or NULL */
    const char** type_params;    /* For generic functions/structs/traits */
    const char** type_param_bounds;
    size_t type_param_count;
    bool is_async;
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
    UfTraitInfo* traits;
    UfTraitImplInfo* trait_impls;
    bool is_in_async_fn;

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
                                      int min_arity,
                                      bool has_rest,
                                      const char* type_annotation,
                                      const char* return_type,
                                      const char** param_types);

void uf_semantic_add_symbol_generic(UfSemanticAnalyzer* analyzer,
                                    const char* name,
                                    UfSymbolKind kind,
                                    SourceSpan span,
                                    int arity,
                                    int min_arity,
                                    bool has_rest,
                                    const char* type_annotation,
                                    const char* return_type,
                                    const char** param_types,
                                    const char** type_params,
                                    const char** type_param_bounds,
                                    size_t type_param_count);

bool uf_analyze_program(UfSemanticAnalyzer* analyzer, UfProgram* program);
bool uf_typecheck_program(UfSemanticAnalyzer* analyzer, UfProgram* program);

#endif /* UF_SEMANTIC_H */
