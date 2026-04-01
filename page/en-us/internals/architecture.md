# Zephyr Language Architecture

This document provides a detailed technical description of the Zephyr programming language implementation, covering the full compilation pipeline, runtime virtual machine, type system, garbage collector, embedding API, and supporting infrastructure.

## 1. High-Level Overview

Zephyr is a statically typed, embeddable scripting language written in C++20. It targets game engine scripting with Lua-level embedding simplicity and modern Rust-like syntax — without borrow checking or lifetime annotations.

**Core design principles:**
- Register-based bytecode VM with superinstruction fusion
- Generational GC with bitmap card table
- Generation-checked host handles for safe C++ ↔ script value exchange
- Coroutines as first-class citizens, not an add-on

## 2. Compilation Pipeline

### 2.1 Lexer
The lexer converts raw UTF-8 source into a flat `Token` stream. All tokens carry a `Span` (byte offset + length) for error reporting.

### 2.2 Parser
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
1. Builds live intervals for every virtual register
2. Linear-scan allocation maps virtual → physical (0–255)
3. Copy propagation eliminates redundant moves
4. Registers that cannot fit into 0–255 emit `R_SPILL_STORE` / `R_SPILL_LOAD` pairs

### 2.7 Superinstruction Fusion

After register allocation, a peephole pass in `optimize_register_bytecode()` iterates `while(changed)`, detecting adjacent and non-adjacent (loop back-edge) patterns and collapsing them into fused opcodes.

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

| Pattern | Fused Opcode | Description |
|---|---|---|
| `R_ADDI_JUMP(reg,+N)` → target: `R_SI_CMPI_JUMP_FALSE(reg,limit)` | `R_SI_ADDI_CMPI_LT_JUMP` | Loop increment + bound check |
| `R_SI_MODI_ADD_STORE(acc,acc,iter,div)` + `R_SI_ADDI_CMPI_LT_JUMP(iter,step,limit)` | `R_SI_LOOP_STEP` | Entire loop step as 1 opcode |

#### R_SI_LOOP_STEP Encoding

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

| Field | Role |
|---|---|
| `dst` (uint8) | Accumulator register |
| `src1` (uint8, reinterpreted as int8) | Loop step |
| `src2` (uint8) | Loop counter register (iter) |
| `operand_a` (uint8) | Modulo divisor (div, 1–255) |
| `ic_slot[15:0]` (uint16) | Loop body start address |
| `ic_slot[31:16]` (int16) | Loop upper bound (limit) |

## 3. Runtime Virtual Machine

### 3.1 VM Structure
The interpreter loop is a `switch`-dispatched decode-execute cycle over the current bytecode chunk. Each call frame holds:
- Pointer to the current `BytecodeChunk`
- Program counter (PC)
- Base register offset into the value stack
- Pointer to the enclosing closure's upvalue array

### 3.2 Instruction Set

All opcodes are prefixed `R_`:

| Opcode | Description |
|---|---|
| `R_LOAD_CONST` / `R_LOAD_INT` | Load constant or immediate integer |
| `R_MOVE` | Copy register to register |
| `R_ADD/SUB/MUL/DIV` | Arithmetic |
| `R_ADDI/MODI` | Arithmetic with immediate operand |
| `R_CMP_EQ/LT/LE` | Comparison → bool register |
| `R_JUMP/R_JUMP_IF_FALSE` | Unconditional / conditional branch |
| `R_CALL` / `R_RETURN` | Function call / return |
| `R_YIELD` / `R_RESUME` | Coroutine suspend / resume |
| `R_GET_FIELD/R_SET_FIELD` | Struct field access |
| `R_BUILD_STRUCT` | Allocate and initialise struct literal |
| `R_SPILL_LOAD/R_SPILL_STORE` | Heap spill for >256 locals |
| `R_SI_ADD/SUB/MUL_STORE` | Fused arithmetic + store |
| `R_SI_CMP_JUMP_FALSE` | Fused compare + branch |
| `R_SI_CMPI_JUMP_FALSE` | Fused immediate compare + branch |
| `R_ADDI_JUMP` | Fused increment + branch |
| `R_SI_ADDI_CMPI_LT_JUMP` | Fused increment + bound check + branch |
| `R_SI_MODI_ADD_STORE` | Fused `dst = acc + (src % imm)` |
| `R_SI_LOOP_STEP` | Full counted loop step |

#### Inline Cache (IC)

`CompactInstruction` carries `mutable ic_shape` and `mutable ic_slot` cache fields.

- **R_BUILD_STRUCT IC**: Caches `StructTypeObject*` after first execution, bypassing type lookup, string parsing, and field validation on subsequent calls.
- **StructTypeObject::cached_shape**: Shape computation (vector alloc + hashmap lookup) runs once per struct type, then is reused directly.

### 3.3 Register Spill Fallback
When a function uses more than 256 local variables, the compiler emits spill opcodes. The spill area is a `GcArray` attached to the call frame. Format version bumped to v2 when spills are present.

### 3.4 Coroutine Model
Each coroutine is a heap-resident `CoroutineFrame` containing:
- Full register bank snapshot
- Operand stack snapshot
- Suspended PC
- Status flags: `.done`, `.suspended`

On yield, `compact_suspended_coroutine()` shrinks the frame's buffer capacity to only what's needed.

### 3.5 Module Bytecode Caching
Compiled `.zph` modules are cached to `.zphc` files alongside the source. The cache stores the source mtime; on next load the mtime is compared and the cache is used or invalidated.

## 4. Value System and Type Hierarchy

### 4.1 Value Representation (NaN-boxing)
All Zephyr values fit in a single 64-bit `ZephyrValue` using NaN-boxing, accommodating pointers, floats, and small integers cleanly.

### 4.2 Built-in Types

| Type | GcObjectType | Notes |
|---|---|---|
| `string` | `GC_STRING` | interned, immutable |
| `Array` | `GC_ARRAY` | dynamic, homogeneous any |
| `StructInstance` | `GC_STRUCT` | named fields |
| `EnumInstance` | `GC_ENUM` | variant + optional payload |
| `Closure` | `GC_CLOSURE` | function + upvalue array |
| `CoroutineFrame` | `GC_COROUTINE` | heap call stack |

### 4.3 Host Handle System
Host objects cross the script boundary as generation-checked tokens rather than GC-managed wrappers. Four handle classes feature different lifespans (Frame, Tick, Persistent, Stable).

## 5. Garbage Collector

### 5.1 Generational Design
Objects are allocated into the **nursery** (young generation). After surviving a configurable number of minor GCs, objects are promoted to **old generation**.

- **Minor GC** — scans only nursery; uses remembered set for old→young edges
- **Major GC** — full tri-color mark-sweep of all generations

### 5.2 Write Barrier and Card Table
Every write to a struct field, array element, closure capture, or global that stores a young object into an old owner trips the write barrier, which marks the owning card in the bitmap card table. Minor GC rescans only dirty card ranges.

### 5.3 Incremental Collection
`gc_step(budget)` performs a bounded slice of GC work (mark or sweep). The host engine calls this once per frame to keep GC pauses below frame budget.

## 6. Type System

### 6.1 Traits and impl
Semacheck pass 2 verifies that every method declared in the trait appears in the `impl` block seamlessly.

### 6.2 Generics
Generic monomorphisation happens at compile time. Where clauses constrain type parameters to types that implement a given trait.

### 6.3 Result\<T\> and Error Propagation
`?` desugars to: if the expression is `Err(e)`, return `Err(e)` from the enclosing function.

## 7. Module System

Module search order:
1. Relative to the importing file's directory
2. Package root (set via `set_package_root()`)
3. Host-registered modules
4. `std/` built-in standard library

## 8. Test Infrastructure & Performance Baseline

Benchmark suite runs 5 acceptance gates with baseline comparison against **`lua_baseline`** ensuring we maintain Lua 5.4-level execution speed or better.

Latest results (2026-04-01):

| Case | Mean | Lua 5.5 | Ratio | Gate |
|---|---|---|---|---|
| module_import | 838 µs | — | — | ✅ |
| hot_arithmetic_loop | ~420 µs | 394 µs | 1.07× | ✅ |
| array_object_churn | ~1,050 µs | 1,909 µs | **0.55×** | ✅ |
| host_handle_entity | ~224 µs | 303 µs | **0.74×** | ✅ |
| coroutine_yield_resume | ~220 µs | 923 µs | **0.24×** | ✅ |
