#ifndef UF_EMIT_C_H
#define UF_EMIT_C_H

#include "../ast/uf_ast.h"
#include <stdio.h>
#include <stdbool.h>

/* Emits formatted C99 source code from an AST program */
bool uf_emit_c_program(const UfProgram* program, FILE* out);
bool uf_emit_c_program_with_path(const UfProgram* program, const char* source_path, FILE* out);

/* Emits C99 source code directly to a destination file */
bool uf_emit_c_to_file(const UfProgram* program, const char* out_c_path);
bool uf_emit_c_to_file_with_path(const UfProgram* program, const char* source_path, const char* out_c_path);

/* Emits C99 and compiles with gcc to a standalone native ELF binary */
bool uf_build_native(const UfProgram* program, const char* out_bin_path);
bool uf_build_native_with_path(const UfProgram* program, const char* source_path, const char* out_bin_path);

/* Emits C99 and compiles with clang to a standalone WebAssembly (.wasm) binary */
bool uf_build_wasm(const UfProgram* program, const char* out_wasm_path);
bool uf_build_wasm_with_path(const UfProgram* program, const char* source_path, const char* out_wasm_path);

/* Emits freestanding C99 configured for embedded/bare-metal runtime */
bool uf_emit_c_program_embedded(const UfProgram* program, const char* source_path, FILE* out);
bool uf_emit_c_to_file_embedded(const UfProgram* program, const char* source_path, const char* out_c_path);

/* Compiles freestanding C99 with gcc (-DUF_EMBEDDED) to test the embedded runtime profile */
bool uf_build_embedded(const UfProgram* program, const char* source_path, const char* out_bin_path);

/* Cross-compiles for ARM Cortex-M using arm-none-eabi-gcc */
bool uf_build_embedded_arm(const UfProgram* program, const char* source_path, const char* out_elf_path);

#endif /* UF_EMIT_C_H */

