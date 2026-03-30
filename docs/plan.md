# Project Zephyr — Implementation Plan

## Completed Waves

| Wave | Contents | Status |
|---|---|---|
| A | Flat Closure, Constant Folding, Write Barrier, int 패스트패스 | ✅ |
| B | 파일 분할, stats CLI | ✅ |
| C | Bitmap Card Table, Lazy Sweep, Adaptive Nursery, String Concat | ✅ |
| D | NaN-boxing, Shape IC, Instruction Compression | ✅ |
| E1 | f"..." interpolation, `?.` chaining, ZephyrClassBinder, Profiler API | ✅ |
| E2 | Pattern Matching 확장, Traits/impl, DAP 서버, VM Snapshot | ✅ |
| F | Superinstruction fusion, GC trace, module bytecode caching, PGO | ✅ |
| G | 진단 메시지, 스택 트레이스, corpus 테스트, 테스트 파일 분리 | ✅ |
| H | String interning, std/math/string/collections, package.toml, CMake | ✅ |
| H.0 | Register VM (R_* opcodes, RegisterAllocator, coroutine 통합) | ✅ |
| K | Generic type parameters | ✅ |
| L | Result<T>, `?` operator, enhanced patterns | ✅ |
| M | Module system (named imports, re-exports) | ✅ |
| N | Lowercase primitive types (int/float/bool/string/void/any) | ✅ |
| O | 2-pass semacheck (hoisting, trait impl completeness) | ✅ |
| P | String interning + trait where clauses | ✅ |
| Q | std/json + std/collections | ✅ |
| R | Associated functions (TypeName::fn) | ✅ |
| S | Error messages + iterator protocol | ✅ |
| T | std/io, std/gc, std/profiler, --profile CLI | ✅ |
| Spill | R_SPILL_LOAD/STORE, >256 locals, format v2 | ✅ |

## Post-Wave Improvements (2026-03-30)

| Task | Status |
|---|---|
| CMake 마이그레이션 완성 (bench/sample/dap 타겟) | ✅ |
| LSP v0.2.0 (signatureHelp, rename, workspace/symbol, hover 타입추론) | ✅ |
| .inl → .cpp 전환 (5개 파일, 독립 TU) | ✅ |
| 빌드 경고 전부 제거 (/utf-8, C4458 수정) | ✅ |
| 불필요 docs 제거 (copilot_scripts 등 63개) | ✅ |
| zephyr_gc.cpp 빈 파일 제거 | ✅ |

## Next Tasks

| # | Task | Priority |
|---|---|---|
| 1 | AOT 컴파일 (LLVM/QBE 백엔드) | Medium |
| 2 | LSP inlay hints | Low |
| 3 | 패키지 매니저 (fetch/lock) | Medium |
| 4 | std/net, std/async | Low |
| 5 | REPL 강화 | Low |
