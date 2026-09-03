#ifndef UF_STDLIB_H
#define UF_STDLIB_H

#include "uf_runtime.h"

struct UfSemanticAnalyzer;

void uf_stdlib_register_runtime(UfRuntime* rt);
void uf_stdlib_register_semantic(struct UfSemanticAnalyzer* analyzer);

#endif /* UF_STDLIB_H */
