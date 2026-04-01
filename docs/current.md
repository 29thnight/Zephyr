# Zephyr — Current State (2026-04-01)

## Project Structure

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
├── lsp_server.cpp       LSP v0.2.0: hover, completion, definition, references,
│                        rename, signatureHelp, workspace/symbol, documentSymbol
└── dap_server.cpp       DAP debug adapter

include/zephyr/api.hpp   Public embedding API
bench/                   Benchmark harness + JSON results
tests/                   Test suite (lexer, compiler, VM, GC, host, perf, corpus)
samples/engine_sample/   Host embedding example
std/                     Standard library (.zph modules)
editors/vscode-zephyr/   VS Code extension
```

## Build System

| System | Status |
|---|---|
| Visual Studio 18 (`Zephyr.sln`) | ✅ All targets build, 0 warnings |
| CMake (`CMakeLists.txt`) | ✅ MSVC + GCC, all targets included |

Compile flags: `/utf-8 /bigobj /permissive-` (MSVC), `-Wall -Wextra` (GCC/Clang)

## Language Features

| Category | Features |
|---|---|
| Declarations | `fn`, `let`, `mut`, `struct`, `enum`, `trait`, `impl`, `import`, `export` |
| Primitive types | `int`, `float`, `bool`, `string`, `void`, `any` |
| Advanced types | `Result<T>`, generics `<T>`, `where T: Trait` clauses |
| Control flow | `if/else`, `while`, `for in`, `break`, `continue`, `return`, `yield`, `match` |
| Operators | `+=`, `-=`, `*=`, `/=`, `?` (error propagation), `?.` (optional chaining) |
| String | `f"..."` interpolation |
| Coroutines | `coroutine fn`, `yield`, `resume`, `.done`, `.suspended` |
| Pattern matching | struct / tuple / enum / range / guard, exhaustiveness checking |
| Associated fns | `TypeName::method()` syntax |
| Modules | named imports, re-exports, package.toml, `set_package_root()` |
| Standard library | `std/math`, `std/string`, `std/collections`, `std/json`, `std/io`, `std/gc`, `std/profiler` |

## VM Architecture

- Register-based bytecode with superinstruction fusion
- Spill fallback for >256 locals (R_SPILL_LOAD / R_SPILL_STORE, format v2)
- Register allocator: live range analysis, copy propagation
- Two-pass semacheck: declaration hoisting + trait impl completeness
- String interning with GC root registration
- Module bytecode caching (mtime-based invalidation)
- Zero AST fallback in Release builds

### 슈퍼인스트럭션 목록

| Opcode | 설명 |
|---|---|
| `R_SI_ADD_STORE` / `R_SI_SUB_STORE` / `R_SI_MUL_STORE` | 연산 + 목적지 이동 |
| `R_SI_CMP_JUMP_FALSE` | 비교 + 조건부 점프 |
| `R_SI_CMPI_JUMP_FALSE` | 즉시값 비교 + 조건부 점프 |
| `R_ADDI_JUMP` | 증가 + 무조건 점프 |
| `R_SI_ADDI_CMPI_LT_JUMP` | 증가 + 상한 비교 + 조건부 점프 (루프 back-edge) |
| `R_SI_MODI_ADD_STORE` | `dst = accum + (src % imm)` |
| `R_SI_LOOP_STEP` | 루프 한 스텝 전체: `accum += iter%div; iter += step; if iter < limit goto body` |

### 인라인 캐시 (IC)

`CompactInstruction`의 `mutable ic_shape` / `mutable ic_slot` 필드를 활용합니다.

- **R_BUILD_STRUCT IC**: 첫 번째 실행 후 `StructTypeObject*`를 캐시. 이후 호출은 타입 탐색·문자열 비교 없이 객체를 직접 할당합니다.
- **StructTypeObject::cached_shape**: Shape 계산(벡터 할당 + 해시맵 조회)을 첫 번째 인스턴스 생성 시 1회로 제한합니다.

## GC

- Generational: nursery (young) + old generation
- Bitmap card table + write barrier for old-to-young edge tracking
- Lazy sweep, adaptive nursery sizing
- Incremental budget (`gc_step()`), full/young explicit triggers
- `start_gc_trace()` / `get_gc_trace_json()` trace export
- GC pause p50/p95/p99 counters

## Host Integration

- Generation-checked handles: `Frame`, `Tick`, `Persistent`, `Stable`
- `ZephyrClassBinder<T>` for C++ type registration
- Versioned serialization: `ZephyrSaveEnvelope` (schema v2)
- `spawn_coroutine()` / `resume()` / `cancel()` / `query_coroutine()`
- `capture_callback()` / `release_callback()`

## Latest Benchmark (5/5 gates PASS)

| Case | Mean | Lua 5.5 | 비율 | Key metric |
|---|---|---|---|---|
| module_import | 838 µs | — | — | 16 opcodes |
| hot_arithmetic_loop | ~420 µs | 394 µs | 1.07× | 1 op/iter (R_SI_LOOP_STEP) |
| array_object_churn | ~1,050 µs | 1,909 µs | **0.55×** ✓ | R_BUILD_STRUCT IC |
| host_handle_entity | ~224 µs | 303 µs | **0.74×** ✓ | — |
| coroutine_yield_resume | ~220 µs | 923 µs | **0.24×** ✓ | — |
| serialization_export | 26.5 µs | — | — | — |
