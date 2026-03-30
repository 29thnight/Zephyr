# Future Direction

## Core Philosophy

**"The lightweight nature of Lua + Modern Syntax + Observable GC/Coroutine Runtime"**

A safe, predictable scripting layer optimized for game engine embedding.

## Performance Targets

We continuously measure against our `lua_baseline` (which represents standard Lua 5.4 performance) to ensure our VM architecture remains competitive for real-time applications.

| Metric | `lua_baseline` | Current Zephyr | Target |
|---|---|---|---|
| arithmetic (ns/op) | 3~5 | ~8 | 4~5 |
| coroutine resume | 200~400 ns | 593 ns | 300~400 ns |
| host resolve | — | 641 ns | < 300 ns |

## Roadmap

### Short-term

| # | Task | Notes |
|---|---|---|
| 1 | AOT compilation | bytecode → native object file, LLVM or QBE backend |
| 2 | JIT (hot path) | opcode_count threshold → ORC JIT, excluding coroutines |
| 3 | LSP inlay hints | `textDocument/inlayHint` — variable type hints |
| 4 | Regression tests | Split zephyr_parser.cpp / zephyr_gc_impl.cpp for TU unit tests |

### Mid-term

| # | Task | Notes |
|---|---|---|
| 5 | Package manager | `package.toml` fetch / lock / publish |
| 6 | Standard library expansion | `std/net`, `std/async`, `std/test` |
| 7 | Enhanced REPL | multiline, history, tab completion |

### Long-term

| # | Task | Notes |
|---|---|---|
| 8 | Self-hosting compiler | Compile Zephyr with Zephyr |
| 9 | WASM target | Emscripten or wasm32-unknown-unknown |

## AOT/JIT Backend Comparison

| Backend | JIT | AOT | C++ Integration | Notes |
|---|---|---|---|---|
| LLVM | ✅ ORC | ✅ | C++ native | Heavy build (~50MB) |
| Cranelift | ✅ | ✅ | Rust FFI needed | Lightweight, Rust toolchain |
| QBE | ❌ | ✅ | C lib | Extremely lightweight (~200KB) |
| MIR/libmir | ✅ | ❌ | C API | Red Hat, Lightweight JIT only |

> Conclusion: Pure game scripting VMs like Gravity/Lua are highly practical even without JIT.
> We recommend adding it only when actual bottlenecks mandate it.

## Non-Goals

- Borrow checking / lifetime annotations
- Async/await paradigm (replaced entirely by coroutines)
- Metaprogramming beyond Reflection / dynamic dispatch
