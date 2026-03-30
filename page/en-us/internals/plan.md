# Project Zephyr — Implementation Plan

## Completed Waves

| Wave | Contents | Status |
|---|---|---|
| A | Flat Closure, Constant Folding, Write Barrier, int fast-path | ✅ |
| B | File splitting, stats CLI | ✅ |
| C | Bitmap Card Table, Lazy Sweep, Adaptive Nursery, String Concat | ✅ |
| D | NaN-boxing, Shape IC, Instruction Compression | ✅ |
| E1 | `f"..."` interpolation, `?.` chaining, ZephyrClassBinder, Profiler API | ✅ |
| E2 | Pattern Matching expansion, Traits/impl, DAP server, VM Snapshot | ✅ |
| F | Superinstruction fusion, GC trace, module bytecode caching, PGO | ✅ |
| G | Diagnostic messages, stack traces, corpus tests, separate test files | ✅ |
| H | String interning, std/math/string/collections, package.toml, CMake | ✅ |
| H.0 | Register VM (R_* opcodes, RegisterAllocator, coroutine integration) | ✅ |
| K | Generic type parameters | ✅ |
| L | `Result<T>`, `?` operator, enhanced patterns | ✅ |
| M | Module system (named imports, re-exports) | ✅ |
| N | Lowercase primitive types (`int`/`float`/`bool`/`string`/`void`/`any`) | ✅ |
| O | 2-pass semacheck (hoisting, trait impl completeness) | ✅ |
| P | String interning + trait where clauses | ✅ |
| Q | std/json + std/collections | ✅ |
| R | Associated functions (`TypeName::fn`) | ✅ |
| S | Error messages + iterator protocol | ✅ |
| T | std/io, std/gc, std/profiler, `--profile` CLI | ✅ |
| Spill | R_SPILL_LOAD/STORE, >256 locals, format v2 | ✅ |

## Post-Wave Improvements (2026-03-30)

| Task | Status |
|---|---|
| CMake migration complete (bench/sample/dap targets) | ✅ |
| LSP v0.2.0 (signatureHelp, rename, workspace/symbol, hover type inference) | ✅ |
| `.inl` → `.cpp` conversion (5 files, independent TUs) | ✅ |
| Eliminate all build warnings (`/utf-8`, C4458 fix) | ✅ |
| Remove unnecessary docs (63 files including copilot_scripts) | ✅ |
| Remove empty `zephyr_gc.cpp` file | ✅ |

## Next Tasks

| # | Task | Priority |
|---|---|---|
| 1 | AOT compilation (LLVM/QBE backend) | Medium |
| 2 | LSP inlay hints | Low |
| 3 | Package manager (fetch/lock) | Medium |
| 4 | `std/net`, `std/async` | Low |
| 5 | Enhanced REPL | Low |
