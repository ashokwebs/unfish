# UNFISH — INTERACTIVE STEP DEBUGGER SPECIFICATION & MANUAL

---

## 1. Executive Summary & Debugger Architecture

The Unfish CLI includes an interactive, source-level step debugger (`src/debugger/uf_debugger.c`):

```bash
unfish debug <program.unfish>
```

Unlike external debuggers that require separate symbol files (`.pdb`, `.dSYM`) or GDB wrappers, the Unfish debugger is **natively integrated into the runtime and virtual machines**:
* **Direct AST & VM Hooking**: Execution pauses at statement boundaries or bytecode line transitions without binary instrumentation.
* **Live In-Scope Expression Evaluation**: Inspect and evaluate arbitrary expressions directly within the local lexical scope of any paused call frame.
* **Transparent Architecture**: Students can inspect high-level variable states alongside raw bytecode instructions and operand stack depths simultaneously.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        DEBUGGER HOOK ARCHITECTURE                      │
├────────────────────────────────────────────────────────────────────────┤
│ Target Code Execution (AST Interpreter or Stack VM)                    │
│       │                                                                │
│       ├── Before Statement Execution (uf_debugger_hook_stmt)           │
│       ▼                                                                │
│ Check Breakpoint Table:                                                │
│   • Has user placed breakpoint at (file, line)?                        │
│   • Is debugger in STEPPING mode?                                      │
│       │                                                                │
│       ├── YES: Suspend Execution -> Launch Debugger Prompt             │
│       └── NO:  Continue Execution at full speed                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Interactive Command Reference

When paused at a breakpoint or step point, the debugger renders a source excerpt and presents the interactive `(unfish-db) ` prompt:

| Command | Shorthand | Arguments | Description |
|---|---|---|---|
| `break` | `b` | `<line>` | Sets a breakpoint at the specified line number |
| `clear` | `d` | `<line>` | Deletes the breakpoint at the specified line number |
| `info break` | `ib` | None | Lists all active breakpoints |
| `step` | `s` | None | Step into: executes one statement, descending into functions |
| `next` | `n` | None | Step over: executes one statement without descending into calls |
| `finish` | `f` | None | Step out: executes until the current function returns to caller |
| `continue` | `c` | None | Resumes continuous execution until next breakpoint or exit |
| `print` | `p` | `<expr>` | Evaluates `<expr>` in the current frame scope and prints result |
| `locals` | `l` | None | Displays all local variables, parameters, and values in frame |
| `stack` | `bt` | None | Displays call stack backtrace with frame depths and lines |
| `disasm` | None | None | Disassembles bytecode for the active function |
| `help` | `h` | None | Displays debugger command reference |
| `quit` | `q` | None | Aborts execution and exits immediately |

---

## 3. Step-by-Step Debugging Walkthrough

Consider a binary search implementation with an off-by-one bug in `search.unfish`:

```unfish
function binary_search(arr, target):
    let low = 0
    let high = len(arr) # BUG: Should be len(arr) - 1

    while low <= high:
        let mid = floor((low + high) / 2)
        if arr[mid] == target:
            return mid
        elif arr[mid] < target:
            low = mid + 1
        else:
            high = mid - 1

    return -1

let data = [10, 20, 30, 40, 50]
say binary_search(data, 99)
```

### Launching the Debugger:
```bash
$ unfish debug search.unfish
Unfish Debugger (v2.1.0) — Type 'help' for commands.
Stopped at entry: search.unfish:1
   1 | function binary_search(arr, target):
   2 |     let low = 0
(unfish-db) break 5
Breakpoint 1 set at search.unfish:5
(unfish-db) continue
Breakpoint 1 hit: search.unfish:5
   4 |     while low <= high:
-> 5 |         let mid = floor((low + high) / 2)
   6 |         if arr[mid] == target:
(unfish-db) locals
  arr = [10, 20, 30, 40, 50]
  target = 99
  low = 0
  high = 5
(unfish-db) print arr[high]
Runtime Error: Index out of bounds (index 5, length 5)
(unfish-db) stack
#0 binary_search(arr, target) at search.unfish:5
#1 <main>() at search.unfish:17
(unfish-db) quit
Debugging session terminated.
```

The debugger immediately uncovers the issue: `high = 5`, which exceeds the bounds of the 5-element array.

---

## 4. Virtual Machine Bytecode Debugging

When debugging virtual machine execution, the debugger exposes the underlying virtual CPU:

```
(unfish-db) disasm
== Disassembly: binary_search ==
0000   2  OP_CONSTANT 0 (0)
0003   |  OP_STORE_LOCAL 2 (low)
0006   3  OP_LOAD_LOCAL 0 (arr)
0009   |  OP_CALL 1 (len)
0012   |  OP_STORE_LOCAL 3 (high)
0015   5  OP_LOAD_LOCAL 2 (low)
0018   |  OP_LOAD_LOCAL 3 (high)
0021   |  OP_LTE
0022   |  OP_JUMP_IF_FALSE 48
```

Developers can step at the opcode level, watching the operand stack mutate after every instruction, providing an unparalleled tool for learning systems architecture and compiler backends.
