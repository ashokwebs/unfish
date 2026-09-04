CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c99 -Isrc/common -Isrc/lexer -Isrc/ast -Isrc/parser -Isrc/semantic -Isrc/runtime -Isrc/interpreter -Isrc/stdlib -Isrc/formatter -Isrc/debugger -Isrc/blocks -Isrc/compiler -Isrc/vm
LDFLAGS = -lm

ASAN_FLAGS = -fsanitize=address,undefined -g

SRCS = src/common/uf_arena.c \
       src/common/uf_string.c \
       src/common/uf_diagnostic.c \
       src/lexer/uf_token.c \
       src/lexer/uf_lexer.c \
       src/ast/uf_ast.c \
       src/parser/uf_parser.c \
       src/semantic/uf_semantic.c \
       src/runtime/uf_value.c \
       src/runtime/uf_env.c \
       src/runtime/uf_runtime.c \
       src/runtime/uf_fiber.c \
       src/runtime/uf_stdlib.c \
       src/runtime/uf_module.c \
       src/stdlib/uf_mod_sys.c \
       src/stdlib/uf_mod_fs.c \
       src/stdlib/uf_mod_random.c \
       src/stdlib/uf_mod_time.c \
       src/stdlib/uf_mod_json.c \
       src/interpreter/uf_interpreter.c \
       src/formatter/uf_formatter.c \
       src/debugger/uf_debugger.c \
       src/blocks/uf_blocks_export.c \
       src/blocks/uf_blocks_import.c \
       src/compiler/uf_chunk.c \
       src/compiler/uf_compiler.c \
       src/compiler/uf_optimize.c \
       src/vm/uf_vm.c \
       src/vm/uf_disasm.c \
       src/codegen/uf_emit_c.c \
       src/lsp/uf_lsp.c

CLI_SRC = src/cli/main.c

BIN_DIR = bin

.PHONY: all asan test test-asan bench clean

all: $(BIN_DIR)/unfish

bench: $(BIN_DIR)/unfish
	@./tools/run_benchmarks.sh $(BIN_DIR)/unfish

$(BIN_DIR)/unfish: $(SRCS) $(CLI_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -O2 $(SRCS) $(CLI_SRC) $(LDFLAGS) -o $(BIN_DIR)/unfish

asan:
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) $(CLI_SRC) $(LDFLAGS) -o $(BIN_DIR)/unfish

test: $(BIN_DIR)/unfish
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_lexer.c $(LDFLAGS) -o $(BIN_DIR)/test_lexer
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_parser.c $(LDFLAGS) -o $(BIN_DIR)/test_parser
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_semantic.c $(LDFLAGS) -o $(BIN_DIR)/test_semantic
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_interpreter.c $(LDFLAGS) -o $(BIN_DIR)/test_interpreter
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_formatter.c $(LDFLAGS) -o $(BIN_DIR)/test_formatter
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_debugger.c $(LDFLAGS) -o $(BIN_DIR)/test_debugger
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_blocks.c $(LDFLAGS) -o $(BIN_DIR)/test_blocks
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_chunk.c $(LDFLAGS) -o $(BIN_DIR)/test_chunk
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_compiler.c $(LDFLAGS) -o $(BIN_DIR)/test_compiler
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_vm.c $(LDFLAGS) -o $(BIN_DIR)/test_vm
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_disasm.c $(LDFLAGS) -o $(BIN_DIR)/test_disasm
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_emit_c.c $(LDFLAGS) -o $(BIN_DIR)/test_emit_c
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_optimize.c $(LDFLAGS) -o $(BIN_DIR)/test_optimize
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_lsp.c $(LDFLAGS) -o $(BIN_DIR)/test_lsp
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_fiber.c $(LDFLAGS) -o $(BIN_DIR)/test_fiber
	$(CC) $(CFLAGS) $(SRCS) tests/unit/test_stress.c $(LDFLAGS) -o $(BIN_DIR)/test_stress
	@echo "=== Running Unit Tests ==="
	@$(BIN_DIR)/test_lexer
	@$(BIN_DIR)/test_parser
	@$(BIN_DIR)/test_semantic
	@$(BIN_DIR)/test_interpreter
	@$(BIN_DIR)/test_formatter
	@$(BIN_DIR)/test_debugger
	@$(BIN_DIR)/test_blocks
	@$(BIN_DIR)/test_chunk
	@$(BIN_DIR)/test_compiler
	@$(BIN_DIR)/test_vm
	@$(BIN_DIR)/test_disasm
	@$(BIN_DIR)/test_emit_c
	@$(BIN_DIR)/test_optimize
	@$(BIN_DIR)/test_lsp
	@$(BIN_DIR)/test_fiber
	@echo "=== Running Stress Tests ==="
	@$(BIN_DIR)/test_stress
	@echo "=== Running Conformance Tests ==="
	@./tools/run_conformance_tests.sh
	@echo "=== Running Differential Tests (Interpreter vs VM) ==="
	@./tools/run_differential_tests.sh

test-asan:
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_lexer.c $(LDFLAGS) -o $(BIN_DIR)/test_lexer
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_parser.c $(LDFLAGS) -o $(BIN_DIR)/test_parser
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_semantic.c $(LDFLAGS) -o $(BIN_DIR)/test_semantic
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_interpreter.c $(LDFLAGS) -o $(BIN_DIR)/test_interpreter
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_formatter.c $(LDFLAGS) -o $(BIN_DIR)/test_formatter
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_debugger.c $(LDFLAGS) -o $(BIN_DIR)/test_debugger
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_blocks.c $(LDFLAGS) -o $(BIN_DIR)/test_blocks
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_chunk.c $(LDFLAGS) -o $(BIN_DIR)/test_chunk
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_compiler.c $(LDFLAGS) -o $(BIN_DIR)/test_compiler
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_vm.c $(LDFLAGS) -o $(BIN_DIR)/test_vm
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_disasm.c $(LDFLAGS) -o $(BIN_DIR)/test_disasm
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_emit_c.c $(LDFLAGS) -o $(BIN_DIR)/test_emit_c
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_optimize.c $(LDFLAGS) -o $(BIN_DIR)/test_optimize
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_lsp.c $(LDFLAGS) -o $(BIN_DIR)/test_lsp
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_fiber.c $(LDFLAGS) -o $(BIN_DIR)/test_fiber
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) tests/unit/test_stress.c $(LDFLAGS) -o $(BIN_DIR)/test_stress
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) $(CLI_SRC) $(LDFLAGS) -o $(BIN_DIR)/unfish
	@echo "=== Running Unit Tests with ASan/UBSan ==="
	@$(BIN_DIR)/test_lexer
	@$(BIN_DIR)/test_parser
	@$(BIN_DIR)/test_semantic
	@$(BIN_DIR)/test_interpreter
	@$(BIN_DIR)/test_formatter
	@$(BIN_DIR)/test_debugger
	@$(BIN_DIR)/test_blocks
	@$(BIN_DIR)/test_chunk
	@$(BIN_DIR)/test_compiler
	@$(BIN_DIR)/test_vm
	@$(BIN_DIR)/test_disasm
	@$(BIN_DIR)/test_emit_c
	@$(BIN_DIR)/test_optimize
	@$(BIN_DIR)/test_lsp
	@$(BIN_DIR)/test_fiber
	@echo "=== Running Stress Tests with ASan/UBSan ==="
	@$(BIN_DIR)/test_stress
	@echo "=== Running Conformance Tests with ASan/UBSan ==="
	@./tools/run_conformance_tests.sh
	@echo "=== Running Differential Tests with ASan/UBSan ==="
	@./tools/run_differential_tests.sh

clean:
	rm -rf $(BIN_DIR)
