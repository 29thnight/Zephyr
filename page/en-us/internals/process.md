# Implementation Log

**Core direction: "The lightweight nature of Lua + Modern Syntax + Observable GC/Coroutine Runtime"**

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

| Date | hot_arithmetic | coroutine/resume | host/resolve | Gates |
|---|---|---|---|---|
| `lua_baseline` | 1,000 ms | 74,813 ns | 33,333 ns | — |
| Wave D | 3.91 ms | 878 ns | 660 ns | 5/5 |
| Register VM | 2.17 ms | 635 ns | 641 ns | 5/5 |
| 2026-03-30 | 1.13 ms | 593 ns | 641 ns | 5/5 |
