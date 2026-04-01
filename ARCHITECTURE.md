# Zephyr Language Architecture

This document provides a detailed technical description of the Zephyr programming language implementation, covering the full compilation pipeline, runtime virtual machine, type system, garbage collector, embedding API, and supporting infrastructure.

## Table of Contents

- [1. High-Level Overview](#1-high-level-overview)
- [2. Compilation Pipeline](#2-compilation-pipeline)
  - [2.1 Lexer](#21-lexer)
  - [2.2 Parser](#22-parser)
  - [2.3 Semantic Analysis — Pass 1](#23-semantic-analysis--pass-1)
  - [2.4 Semantic Analysis — Pass 2](#24-semantic-analysis--pass-2)
  - [2.5 Code Generation](#25-code-generation)
  - [2.6 Register Allocator](#26-register-allocator)
  - [2.7 Superinstruction Fusion](#27-superinstruction-fusion)
- [3. Runtime Virtual Machine](#3-runtime-virtual-machine)
  - [3.1 VM Structure](#31-vm-structure)
  - [3.2 Instruction Set](#32-instruction-set)
    - [3.2.1 Inline Cache (IC)](#321-inline-cache-ic)
  - [3.3 Register Spill Fallback](#33-register-spill-fallback)
  - [3.4 Coroutine Model](#34-coroutine-model)
  - [3.5 Module Bytecode Caching](#35-module-bytecode-caching)
- [4. Value System and Type Hierarchy](#4-value-system-and-type-hierarchy)
  - [4.1 Value Representation (NaN-boxing)](#41-value-representation-nan-boxing)
  - [4.2 Object Header and GC Metadata](#42-object-header-and-gc-metadata)
  - [4.3 Built-in Types](#43-built-in-types)
  - [4.4 Host Handle System](#44-host-handle-system)
- [5. Garbage Collector](#5-garbage-collector)
  - [5.1 Generational Design](#51-generational-design)
  - [5.2 Write Barrier and Card Table](#52-write-barrier-and-card-table)
  - [5.3 Incremental Collection](#53-incremental-collection)
  - [5.4 GC Diagnostics](#54-gc-diagnostics)
- [6. Type System](#6-type-system)
  - [6.1 Traits and impl](#61-traits-and-impl)
  - [6.2 Generics](#62-generics)
  - [6.3 Result\<T\> and Error Propagation](#63-resultt-and-error-propagation)
- [7. Module System](#7-module-system)
- [8. Standard Library](#8-standard-library)
- [9. Embedding API](#9-embedding-api)
- [10. CLI](#10-cli)
- [11. Build System](#11-build-system)
- [12. LSP and DAP Servers](#12-lsp-and-dap-servers)
- [13. Test Infrastructure](#13-test-infrastructure)

---

## 1. High-Level Overview

Zephyr is a statically typed, embeddable scripting language written in C++20. It targets game engine scripting with Lua-level embedding simplicity and modern Rust-like syntax — without borrow checking or lifetime annotations.

**Core design principles:**
- Register-based bytecode VM with superinstruction fusion
- Generational GC with bitmap card table
- Generation-checked host handles for safe C++ ↔ script value exchange
- Coroutines as first-class citizens, not an add-on

### Source Layout

```
src/
├── zephyr.cpp           VM public API: compile_bytecode_function, compile_module_bytecode
├── zephyr_parser.cpp    Lexer + Parser + Runtime::parse_source()
├── zephyr_gc_impl.cpp   Generational GC: nursery, old-gen, card table, write barrier
├── zephyr_internal.hpp  Shared internal types, macros, forward declarations
├── zephyr_lexer.hpp     Lexer class
├── zephyr_types.hpp     GC object types, value representation, host handle infrastructure
└── zephyr_compiler.hpp  BytecodeOp, IR, BytecodeCompiler

cli/
├── main.cpp             run / check / repl / stats / dump-bytecode / bench / lsp / dap
├── lsp_server.cpp       LSP v0.2.0 server
└── dap_server.cpp       DAP debug adapter

include/zephyr/api.hpp   Public embedding API
bench/                   Benchmark harness + JSON results
tests/                   Test suite
std/                     Standard library (.zph modules)
editors/vscode-zephyr/   VS Code extension
```

### Full Pipeline

```
Source Code
    │
    ▼
┌──────────┐
│  Lexer   │  Character stream → Token stream
└────┬─────┘
     ▼
┌──────────┐
│  Parser  │  Token stream → Abstract Syntax Tree
└────┬─────┘
     ▼
┌──────────────┐
│ Semacheck 1  │  Hoist declarations, build symbol tables
└──────┬───────┘
       ▼
┌──────────────┐
│ Semacheck 2  │  Resolve identifiers, validate trait impls, detect upvalues
└──────┬───────┘
       ▼
┌──────────────┐
│   Codegen    │  AST → IR (virtual registers)
└──────┬───────┘
       ▼
┌──────────────────┐
│ RegisterAllocator│  Live range analysis, copy propagation, spill emit
└──────┬───────────┘
       ▼
┌──────────────────┐
│  SI Fusion       │  Superinstruction pattern matching
└──────┬───────────┘
       ▼
┌──────────────────┐
│  Bytecode Chunk  │  Serialized, cacheable, format v2
└──────────────────┘
```

---

## 2. Compilation Pipeline

### 2.1 Lexer

**File:** `src/zephyr_lexer.hpp`

The lexer converts raw UTF-8 source into a flat `Token` stream. All tokens carry a `Span` (byte offset + length) for error reporting.

Key token classes:

| Class | Examples |
|---|---|
| Keywords | `fn`, `let`, `mut`, `struct`, `enum`, `trait`, `impl`, `import`, `export` |
| Literals | integer, float, string, bool, `nil` |
| Operators | `+`, `-`, `*`, `/`, `?.`, `?`, `->` |
| Delimiters | `(`, `)`, `{`, `}`, `[`, `]`, `,`, `:`, `;` |
| Identifiers | user-defined names |

String interpolation (`f"..."`) is tokenised into alternating string-literal and expression segments at lex time.

### 2.2 Parser

**File:** `src/zephyr_parser.cpp`

The parser is a hand-written recursive-descent parser that produces an AST. It handles:
- Pratt (TDOP) parsing for expressions with precedence climbing
- Generic parameter lists (`<T, U>`) and where clauses
- `match` exhaustiveness hints embedded in the AST

### 2.3 Semantic Analysis — Pass 1

Hoists all top-level `fn`, `struct`, `enum`, `trait`, `impl` declarations into the module symbol table so forward references resolve cleanly.

### 2.4 Semantic Analysis — Pass 2

- Resolves all identifiers against the symbol table
- Checks that `impl` blocks satisfy their trait's required method set
- Detects upvalue captures for closures
- Infers types for `let x = <literal>` bindings

### 2.5 Code Generation

The compiler walks the resolved AST and emits IR instructions with virtual (unlimited) register numbers. Each IR instruction maps 1-to-1 to a future `R_*` opcode.

### 2.6 Register Allocator

**File:** `src/zephyr_compiler.hpp` (`RegisterAllocator`)

1. Builds live intervals for every virtual register
2. Linear-scan allocation maps virtual → physical (0–255)
3. Copy propagation eliminates redundant moves
4. Registers that cannot fit into 0–255 emit `R_SPILL_STORE` / `R_SPILL_LOAD` pairs

### 2.7 Superinstruction Fusion

After register allocation, a peephole pass in `optimize_register_bytecode()` collapses common instruction patterns into fused opcodes. The pass iterates `while(changed)` and handles both adjacent and non-adjacent (loop back-edge) patterns.

#### Adjacent Patterns

| Pattern | Fused Opcode | Description |
|---|---|---|
| `R_ADD/SUB/MUL` + `R_MOVE(dst, result)` | `R_SI_ADD/SUB/MUL_STORE` | Arithmetic + destination move |
| `R_MODI(tmp,src,imm)` + `R_MOVE(dst,tmp)` | `R_MODI(dst,src,imm)` | Eliminate temporary register |
| `R_ADDI(dst,src,imm)` + `R_JUMP(target)` | `R_ADDI_JUMP` | Increment + unconditional branch |
| `R_CMP*` + `R_JUMP_IF_FALSE` | `R_SI_CMP_JUMP_FALSE` | Compare + conditional branch |
| `R_LOAD_INT(tmp,imm)` + `R_SI_CMP_JUMP_FALSE` | `R_SI_CMPI_JUMP_FALSE` | Immediate compare + conditional branch |
| `R_MODI(tmp,src,imm)` + `R_SI_ADD_STORE(dst,acc,tmp)` | `R_SI_MODI_ADD_STORE` | `dst = acc + (src % imm)` |

#### Non-Adjacent Loop Patterns

Detected via `R_ADDI_JUMP`'s `ic_slot` field (jump target index):

| Pattern | Fused Opcode | Description |
|---|---|---|
| `R_ADDI_JUMP(reg,+N)` → target: `R_SI_CMPI_JUMP_FALSE(reg,limit)` | `R_SI_ADDI_CMPI_LT_JUMP` | Loop increment + bound check |
| `R_SI_MODI_ADD_STORE(acc,acc,iter,div)` + `R_SI_ADDI_CMPI_LT_JUMP(iter,step,limit)` | `R_SI_LOOP_STEP` | Entire loop step as 1 opcode |

#### R_SI_LOOP_STEP

The most aggressively fused superinstruction. Executes three operations atomically:

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

**Encoding** (within the 24-byte `CompactInstruction`):

| Field | Role |
|---|---|
| `dst` (uint8) | Accumulator register |
| `src1` (uint8, reinterpreted as int8) | Loop step (`step`) |
| `src2` (uint8) | Loop counter register (`iter`) |
| `operand_a` (uint8) | Modulo divisor (`div`, 1–255) |
| `ic_slot[15:0]` (uint16) | Loop body start address |
| `ic_slot[31:16]` (int16) | Loop upper bound (`limit`) |

**Fusion conditions:** `dst==src1` (self-accumulating), same iter register, `body_start ≤ 0xFFFF`, `step ∈ [-128,127]`, `limit ∈ [-32768,32767]`

**Effect on `hot_arithmetic`:**

| Stage | ops/iter | Time |
|---|---|---|
| Base register VM | ~6 | 2,170 µs |
| +R_SI_MODI_ADD_STORE | 3 | 692 µs |
| +R_SI_ADDI_CMPI_LT_JUMP | 2 | 516 µs |
| +R_SI_LOOP_STEP | **1** | **~420 µs** |

---

## 3. Runtime Virtual Machine

### 3.1 VM Structure

The interpreter loop is a `switch`-dispatched decode-execute cycle over the current bytecode chunk. Each call frame holds:
- Pointer to the current `BytecodeChunk`
- Program counter (PC)
- Base register offset into the value stack
- Pointer to the enclosing closure's upvalue array

### 3.2 Instruction Set

All opcodes are prefixed `R_` (register-based). Key opcodes:

| Opcode | Description |
|---|---|
| `R_LOAD_CONST` | Load constant pool entry into register |
| `R_LOAD_INT` | Load immediate integer into register |
| `R_MOVE` | Copy register to register |
| `R_ADD/SUB/MUL/DIV` | Arithmetic |
| `R_ADDI/MODI` | Arithmetic with immediate operand |
| `R_CMP_EQ/LT/LE` | Comparison → bool register |
| `R_JUMP/R_JUMP_IF_FALSE` | Unconditional / conditional branch |
| `R_CALL` | Call function in register |
| `R_RETURN` | Return value from current frame |
| `R_YIELD` | Suspend coroutine, return value to caller |
| `R_RESUME` | Resume suspended coroutine |
| `R_GET_FIELD/R_SET_FIELD` | Struct field access |
| `R_BUILD_STRUCT` | Allocate and initialise struct literal |
| `R_SPILL_LOAD/R_SPILL_STORE` | Heap spill for >256 locals |
| `R_SI_ADD/SUB/MUL_STORE` | Fused arithmetic + destination store |
| `R_SI_CMP_JUMP_FALSE` | Fused compare + conditional branch |
| `R_SI_CMPI_JUMP_FALSE` | Fused immediate compare + conditional branch |
| `R_ADDI_JUMP` | Fused increment + unconditional branch |
| `R_SI_ADDI_CMPI_LT_JUMP` | Fused increment + bound check + conditional branch |
| `R_SI_MODI_ADD_STORE` | Fused `dst = acc + (src % imm)` |
| `R_SI_LOOP_STEP` | Entire counted loop step: accum, increment, branch |

### 3.2.1 Inline Cache (IC)

`CompactInstruction` carries two mutable cache fields:

```cpp
mutable Shape*   ic_shape;   // cached Shape* or type pointer
mutable uint32_t ic_slot;    // cached field index or state flag
```

**R_BUILD_STRUCT IC** — on the first execution, checks that field declaration order matches the struct type's field order, then stores `StructTypeObject*` in `ic_shape` and sets `ic_slot = 1`. Subsequent calls skip `parse_type_name`, `expect_struct_type`, `field_slot` lookup, `enforce_type`, and `validate_handle_store`, reducing struct literal creation from a 6-step string-traversal path to a 4-step direct-allocation path.

**StructTypeObject::cached_shape** — `initialize_struct_instance()` previously called `shape_for_struct_type()` on every instantiation (vector alloc + string concat + hashmap lookup). The first call now populates `type->cached_shape`; subsequent calls use the cached pointer directly.

### 3.3 Register Spill Fallback

When a function uses more than 256 local variables, the compiler emits spill opcodes. The spill area is a `GcArray` attached to the call frame. Format version bumped to v2 when spills are present.

### 3.4 Coroutine Model

Each coroutine is a heap-resident `CoroutineFrame` containing:
- Full register bank snapshot
- Operand stack snapshot
- Suspended PC
- Status flags: `.done`, `.suspended`

`R_YIELD` serialises the current frame to heap; `R_RESUME` deserialises and continues. Nested helper calls within a coroutine are fully supported — the entire call chain suspends together.

On yield, `compact_suspended_coroutine()` shrinks the frame's buffer capacity to only what's needed.

### 3.5 Module Bytecode Caching

Compiled `.zph` modules are cached to `.zphc` files alongside the source. The cache stores the source mtime; on next load the mtime is compared and the cache is used or invalidated.

---

## 4. Value System and Type Hierarchy

### 4.1 Value Representation (NaN-boxing)

**File:** `src/zephyr_types.hpp`

All Zephyr values fit in a single 64-bit `ZephyrValue` using NaN-boxing:

```
 63      51  50     0
┌──────────┬────────────────────────────────────────────────────┐
│ NaN bits │  payload (pointer or small integer or float bits)  │
└──────────┴────────────────────────────────────────────────────┘
```

| Tag | Type |
|---|---|
| `0` | IEEE-754 double (float) |
| quiet NaN + tag `1` | int (48-bit) |
| quiet NaN + tag `2` | bool |
| quiet NaN + tag `3` | nil |
| quiet NaN + tag `4`+ | GC pointer |

### 4.2 Object Header and GC Metadata

Every heap object (`GcObject`) starts with:

```cpp
struct GcObjectHeader {
    GcObjectType type;      // 1 byte
    uint8_t      gc_color;  // white / gray / black
    uint8_t      gen;       // 0 = young, 1 = old
    uint8_t      flags;
    uint32_t     size;
};
```

### 4.3 Built-in Types

| Type | GcObjectType | Notes |
|---|---|---|
| `string` | `GC_STRING` | interned, immutable |
| `Array` | `GC_ARRAY` | dynamic, homogeneous any |
| `StructInstance` | `GC_STRUCT` | named fields |
| `EnumInstance` | `GC_ENUM` | variant + optional payload |
| `Closure` | `GC_CLOSURE` | function + upvalue array |
| `CoroutineFrame` | `GC_COROUTINE` | heap call stack |

### 4.4 Host Handle System

Host objects cross the script boundary as generation-checked tokens rather than GC-managed wrappers. Four handle classes:

| Class | Lifespan | Serializable |
|---|---|---|
| `Frame` | current call frame | No |
| `Tick` | current engine tick | No |
| `Persistent` | across calls | No |
| `Stable` | across scene boundaries | Yes |

---

## 5. Garbage Collector

### 5.1 Generational Design

**File:** `src/zephyr_gc_impl.cpp`

Objects are allocated into the **nursery** (young generation). After surviving a configurable number of minor GCs, objects are promoted to **old generation**.

- **Minor GC** — scans only nursery; uses remembered set for old→young edges
- **Major GC** — full tri-color mark-sweep of all generations

### 5.2 Write Barrier and Card Table

Every write to a struct field, array element, closure capture, or global that stores a young object into an old owner trips the write barrier, which marks the owning card in the bitmap card table.

Minor GC rescans only dirty card ranges instead of full old-gen containers.

### 5.3 Incremental Collection

`gc_step(budget)` performs a bounded slice of GC work (mark or sweep). The host engine calls this once per frame to keep GC pauses below frame budget.

Exposed stats: p50 / p95 / p99 pause counters.

### 5.4 GC Diagnostics

```cpp
rt.start_gc_trace();
// ... run script ...
std::string json = rt.get_gc_trace_json();
```

The JSON trace contains timestamped nursery/old-gen allocation events, pause durations, and promotion counts.

---

## 6. Type System

### 6.1 Traits and impl

```zephyr
trait Drawable {
    fn draw(self) -> void;
    fn area(self) -> float;
}

struct Circle { radius: float }

impl Drawable for Circle {
    fn draw(self) -> void { print(f"Circle r={self.radius}"); }
    fn area(self) -> float { return 3.14159 * self.radius * self.radius; }
}
```

Semacheck pass 2 verifies that every method declared in the trait appears in the `impl` block.

### 6.2 Generics

```zephyr
fn max<T>(a: T, b: T) -> T where T: Comparable {
    if a > b { return a; }
    return b;
}
```

Generic monomorphisation happens at compile time. Where clauses constrain type parameters to types that implement a given trait.

### 6.3 Result\<T\> and Error Propagation

```zephyr
fn parse_int(s: string) -> Result<int> { ... }

fn load() -> Result<int> {
    let n = parse_int("42")?;  // early-return on Err
    return Ok(n * 2);
}
```

`?` desugars to: if the expression is `Err(e)`, return `Err(e)` from the enclosing function.

---

## 7. Module System

```zephyr
import "std/math";
import "utils" as u;
import { sqrt, abs } from "std/math";
export fn greet(name: string) { ... }
```

Module search order:
1. Relative to the importing file's directory
2. Package root (set via `set_package_root()`)
3. Host-registered modules
4. `std/` built-in standard library

---

## 8. Standard Library

| Module | Key exports |
|---|---|
| `std/math` | `sin`, `cos`, `sqrt`, `abs`, `floor`, `ceil`, `clamp`, `lerp`, `PI` |
| `std/string` | `split`, `trim`, `replace`, `to_upper`, `to_lower` |
| `std/collections` | `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>` |
| `std/json` | `parse(s) -> Result<any>`, `stringify(v) -> string` |
| `std/io` | `read_file`, `write_file`, `read_lines` |
| `std/gc` | `collect()`, `collect_young()`, `step()`, `stats()` |
| `std/profiler` | `start()`, `stop()`, `report()` |

---

## 9. Embedding API

**File:** `include/zephyr/api.hpp`

```cpp
#include <zephyr/api.hpp>

ZephyrVM vm;
vm.set_package_root("game/scripts/");
auto rt = vm.create_runtime();

// Compile and run
auto chunk = vm.compile_bytecode_function(source, "main");
rt.execute(chunk);

// Host type registration
ZephyrClassBinder<Entity> binder("Entity");
binder.method("get_pos", [](Entity& e) { return e.pos; });
vm.register_class(binder);

// Coroutines
auto co = rt.spawn_coroutine("update_ai");
while (!rt.query_coroutine(co).done) {
    rt.resume(co, {});
    engine.tick();
}

// GC integration
rt.advance_tick();
rt.gc_step(1000);   // 1000 µs budget
```

See [Embedding](docs/embedding.md) for full examples.

---

## 10. CLI

```
zephyr run <file>          Execute a script
zephyr check <file>        Typecheck without running
zephyr repl                Interactive REPL
zephyr dump-bytecode <f>   Disassemble bytecode
zephyr stats <file>        Print compilation stats
zephyr bench               Run benchmark suite
zephyr lsp                 Start LSP server (stdio)
zephyr dap                 Start DAP debug adapter (stdio)
```

`--profile` flag enables the sampling profiler and writes `zephyr_profile.json`.

---

## 11. Build System

| System | Command |
|---|---|
| Visual Studio 18 | Open `Zephyr.sln`, Build All |
| CMake + MSVC | `cmake -B build && cmake --build build --config Release` |
| CMake + GCC | `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build` |

Key CMake options:

| Option | Default | Description |
|---|---|---|
| `ZEPHYR_BUILD_CLI` | ON | Build `zephyr` executable |
| `ZEPHYR_BUILD_TESTS` | ON | Build `zephyr_tests` |
| `ZEPHYR_BUILD_BENCH` | ON | Build `zephyr_bench` |
| `ZEPHYR_BUILD_SAMPLES` | ON | Build engine sample |
| `ZEPHYR_ENABLE_PGO` | OFF | MSVC PGO instrumentation |

---

## 12. LSP and DAP Servers

**File:** `cli/lsp_server.cpp`

LSP v0.2.0 capabilities:

| Request | Status |
|---|---|
| `textDocument/hover` | type-inferred display |
| `textDocument/completion` | keyword + symbol completions |
| `textDocument/definition` | go-to definition |
| `textDocument/references` | find all references |
| `textDocument/rename` | workspace-wide rename |
| `textDocument/signatureHelp` | function parameter hints |
| `textDocument/documentSymbol` | outline |
| `workspace/symbol` | cross-document symbol search |

**File:** `cli/dap_server.cpp` — DAP debug adapter for breakpoint-based debugging.

VS Code extension: `editors/vscode-zephyr/`

---

## 13. Test Infrastructure

```
tests/
├── test_main.cpp     Test runner entry point
├── test_lexer.cpp    Lexer unit tests
├── test_compiler.cpp Compiler unit tests
├── test_vm.cpp       VM execution tests
├── test_gc.cpp       GC correctness tests
├── test_host.cpp     Host API integration tests
├── test_perf.cpp     Performance regression tests
└── test_corpus.cpp   Corpus-driven tests (tests/corpus/*.zph)
```

Benchmark suite (`bench/`): 5 acceptance gates with baseline comparison against `bench/results/v1_baseline.json`. Non-zero exit on gate failure for CI regression gating.

Latest results (2026-04-01):

| Case | Mean | Lua 5.5 | Ratio | Gate |
|---|---|---|---|---|
| module_import | 838 µs | — | — | ✅ |
| hot_arithmetic_loop | ~420 µs | 394 µs | 1.07× | ✅ |
| array_object_churn | ~1,050 µs | 1,909 µs | **0.55×** | ✅ |
| host_handle_entity | ~224 µs | 303 µs | **0.74×** | ✅ |
| coroutine_yield_resume | ~220 µs | 923 µs | **0.24×** | ✅ |
