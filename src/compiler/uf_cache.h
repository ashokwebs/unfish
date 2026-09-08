#ifndef UF_CACHE_H
#define UF_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../ast/uf_ast.h"
#include "uf_chunk.h"
#include "../vm2/uf_regvm.h"
#include "../runtime/uf_runtime.h"

/* Compute source hash (FNV-1a 64-bit over source text) */
uint64_t uf_cache_hash_source(const char* source, size_t length);

/* Serialize a stack bytecode function to a file */
bool uf_cache_write_stack(const char* cache_path, UfProgram* program, UfBytecodeFunction* fn, uint64_t source_hash);

/* Deserialize a stack bytecode function from a file; returns NULL if stale or corrupt */
UfBytecodeFunction* uf_cache_read_stack(UfRuntime* rt, const char* cache_path, uint64_t source_hash);

/* Serialize a register bytecode function to a file */
bool uf_cache_write_reg(const char* cache_path, UfProgram* program, UfRegFunction* fn, uint64_t source_hash);

/* Deserialize a register bytecode function from a file; returns NULL if stale or corrupt */
UfRegFunction* uf_cache_read_reg(UfRuntime* rt, const char* cache_path, uint64_t source_hash);

/* Get cache path for a source file. If is_regvm is true, uses .ufrc extension, else .ufc.
 * Caller must free the returned string. */
char* uf_cache_path_for(const char* source_path, bool is_regvm);

#endif /* UF_CACHE_H */
