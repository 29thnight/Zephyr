# Implementation Log

**Core direction: "The lightweight nature of Lua + Modern Syntax + Observable GC/Coroutine Runtime"**

---

## 2026-04-01

### VM Optimization — Superinstruction Fusion & Inline Cache

#### R_BUILD_STRUCT Inline Cache + StructTypeObject Shape Cache

Profiling revealed the real bottleneck of `array_object_churn` was not GC but `R_BUILD_STRUCT`'s per-call string-based type lookup. Two caches were introduced:

- **IC fast-path**: Uses `ic_shape` (StructTypeObject*) and `ic_slot==1` (field-order match flag) to bypass `parse_type_name`, `expect_struct_type`, `field_slot`, `enforce_type`, `validate_handle_store`, and temp vector allocation from the second call onward.
- **StructTypeObject::cached_shape**: Added `mutable Shape* cached_shape = nullptr`; `initialize_struct_instance()` now computes the shape only once per struct type.
- **Single-pass field init**: IC warm path uses `reserve(N)` + `push_back` × N instead of `assign(N, nil)` + overwrite, eliminating N redundant nil writes.

Result: array_object_churn **2,330 µs → 1,050 µs (−56%)**, **~2× faster than Lua 5.5** (1,909 µs).

#### R_SI_LOOP_STEP Superinstruction

Fuses `R_SI_MODI_ADD_STORE` + `R_SI_ADDI_CMPI_LT_JUMP` into a single opcode:

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

Encoding: `{dst=accum, src1=step(int8), src2=iter, operand_a=div}` + `ic_slot = (int16_limit << 16) | uint16_body_start`

Result: hot_arithmetic **6 ops/iter → 1 op/iter**, **2,170 µs → 420 µs**, **1.07× Lua 5.5** (394 µs).

#### UB Fix (Signed left-shift)

Fixed undefined behavior at 4 sites in bit-packing functions by casting signed int8/int16 values through `uint32_t` before shifting.

#### Benchmark Progression

| Stage | hot_arithmetic | array_object_churn |
|---|---|---|
| 2026-03-30 baseline | 1,130 µs | 4,310 µs |
| +R_SI_ADDI_CMPI_LT_JUMP | 535 µs | — |
| +R_BUILD_STRUCT IC + Shape cache | — | 1,050 µs |
| **+R_SI_LOOP_STEP** | **420 µs** | — |
| **Lua 5.5 (reference)** | 394 µs | 1,909 µs |

---

## 2026-03-30 (Today)

### CMake Migration
- Added `zephyr_bench` target (`ZEPHYR_BUILD_BENCH`)
- Added `zephyr_engine_sample` target (`ZEPHYR_BUILD_SAMPLES`)
- Fixed missing `dap_server.cpp`
- Unified `/utf-8 /bigobj /permissive-` flags

### LSP v0.2.0 Enhancements
- `textDocument/signatureHelp` — function parameter hints
- `textDocument/rename` — bulk rename across workspace
- `workspace/symbol` — symbol search
- Improved hover type inference — `let x = 42` → detects `int`
- Registered capabilities: `renameProvider`, `workspaceSymbolProvider`, `signatureHelpProvider`
- Server version 0.1.0 → 0.2.0

### `.inl` → `.cpp` Conversion
- `zephyr_lexer.inl` → `zephyr_lexer.hpp` (`#pragma once` + comments)
- `zephyr_types.inl` → `zephyr_types.hpp`
- `zephyr_compiler.inl` → `zephyr_compiler.hpp`
- `zephyr_parser.inl` → `zephyr_parser.cpp` (Independent TU)
- `zephyr_gc.inl` → `zephyr_gc_impl.cpp` (Independent TU)
- Moved `Runtime::parse_source()` to `zephyr_parser.cpp`

### Build Warning Elimination
- C4819 (code page) → added `/utf-8` to vcxproj + CMakeLists.txt
- C4458 (name shadowing) → renamed structured binding variables
- **Result: 0 Warnings**

### Cleanup
- Deleted empty `zephyr_gc.cpp` wrapper
- Deleted `docs/copilot_scripts/` (58 completed implementation scripts)
- Deleted outdated GC and codebase reports

---

## 2026-03-28

### Register VM Completion (master branch)
- `R_*` opcodes + `R_SI_*` superinstructions
- RegisterAllocator + live range analysis, copy propagation
- `execute_register_bytecode()` + `execute_register_bytecode_coro()`
- Unified coroutine registers
- Optimized `compact_suspended_coroutine()`
- **Benchmark**: hot_arithmetic 2.17ms (~5.4 ns/op), coroutine 635 ns/resume

### Register Spill Fallback
- `R_SPILL_LOAD` / `R_SPILL_STORE` opcodes
- Automatic heap spill emit when slot >= 256
- Added `/bigobj` build flag, format version bumped 1→2

---

## Benchmark History

| Date | hot_arithmetic | array_churn | coroutine/resume | host/resolve | Gates |
|---|---|---|---|---|---|
| v1 baseline | 1,000 ms | — | 74,813 ns | 33,333 ns | — |
| Wave D | 3.91 ms | — | 878 ns | 660 ns | 5/5 |
| Register VM | 2.17 ms | — | 635 ns | 641 ns | 5/5 |
| 2026-03-30 | 1.13 ms | 4,310 µs | 593 ns | 641 ns | 5/5 |
| **2026-04-01** | **~420 µs** | **~1,050 µs** | ~220 µs | ~224 µs | 5/5 |
| Lua 5.5 (ref) | 394 µs | 1,909 µs | 923 µs | 303 µs | — |
