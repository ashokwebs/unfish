#ifndef UF_VM_H
#define UF_VM_H

#include "../compiler/uf_chunk.h"
#include "../compiler/uf_compiler.h"
#include "../runtime/uf_runtime.h"
#include "../runtime/uf_value.h"
#include "../interpreter/uf_interpreter.h"
#include <stdbool.h>

#define UF_VM_FRAMES_MAX 256
#define UF_VM_STACK_MAX 4096

struct UfUpvalueCell {
    UfObj obj;
    UfValue* location;
    UfValue closed;
    struct UfUpvalueCell* next;
};

struct UfClosureObject {
    UfObj obj;
    UfBytecodeFunction* function;
    UfUpvalueCell** upvalues;
    size_t upvalue_count;
};

typedef struct UfVMFrame {
    UfClosureObject* closure;
    uint8_t* ip;
    UfValue* slots;
} UfVMFrame;

#define UF_VM_HANDLERS_MAX 64

typedef struct UfVMHandler {
    int frame_index;
    uint8_t* catch_ip;
    UfValue* stack_top;
} UfVMHandler;

typedef struct UfVM {
    UfVMFrame frames[UF_VM_FRAMES_MAX];
    int frame_count;

    UfValue stack[UF_VM_STACK_MAX];
    UfValue* stack_top;

    UfVMHandler handlers[UF_VM_HANDLERS_MAX];
    int handler_count;

    UfUpvalueCell* open_upvalues;
    UfRuntime* rt;
    bool had_error;

    bool trace_execution;
    uint64_t total_instructions;
    size_t peak_stack_depth;
    size_t peak_frame_depth;
} UfVM;

void uf_vm_init(UfVM* vm, UfRuntime* rt);
void uf_vm_free(UfVM* vm);

static inline void uf_vm_push(UfVM* vm, UfValue value) {
    *vm->stack_top++ = value;
}

static inline UfValue uf_vm_pop(UfVM* vm) {
    return *--vm->stack_top;
}

static inline UfValue uf_vm_peek(UfVM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

UfClosureObject* uf_closure_new(UfRuntime* rt, UfBytecodeFunction* function);
UfValue uf_val_closure(UfRuntime* rt, UfClosureObject* closure);

UfInterpretResult uf_vm_run(UfVM* vm, UfBytecodeFunction* function);
UfValue uf_vm_run_closure(UfVM* vm, UfClosureObject* closure, size_t argc, UfValue* args);

#endif /* UF_VM_H */
