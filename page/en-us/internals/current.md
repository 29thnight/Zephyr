# Zephyr — Current State

## Project Structure

```
src/
├── zephyr.cpp           VM public API
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

- Register-based bytecode with superinstruction fusion (SI_ADD_STORE, SI_CMP_JUMP, etc.)
- Spill fallback for >256 locals (R_SPILL_LOAD / R_SPILL_STORE, format v2)
- Register allocator: live range analysis, copy propagation
- Two-pass semacheck: declaration hoisting + trait impl completeness
- String interning with GC root registration
- Module bytecode caching (mtime-based invalidation)
- Zero AST fallback in Release builds

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

All runtime metrics represent the latest baseline comparison against **`lua_baseline`** ensuring we maintain Lua 5.4-level execution speeds or better:

| Case | Mean | Key metric vs `lua_baseline` |
|---|---|---|
| module_import | 838 µs | 16 opcodes |
| hot_arithmetic_loop | 1.13 ms | Meets pure arithmetic budget goals |
| array_object_churn | 4.31 ms | 0 full GC cycles |
| host_handle_entity | 1.92 ms | 641 ns/resolve |
| coroutine_yield_resume | 238 µs | 593 ns/resume |
| serialization_export | 26.5 µs | — |
