# Zephyr Mini Spec

## Goals

- Rust-like surface syntax without borrow checking or lifetime annotations
- Lua-like immediacy for gameplay scripts
- Embeddable C++ runtime with explicit host bindings
- Register-based bytecode VM with superinstruction fusion; release builds stay bytecode-only

## Surface Syntax

### Declarations

| Keyword | Purpose |
|---|---|
| `fn` | Function declaration |
| `let` / `mut` | Immutable / mutable binding |
| `struct` | Struct type |
| `enum` | Enum type |
| `trait` | Trait definition |
| `impl` | Trait implementation for a type |
| `import` / `export` | Module imports and exports |

### Primitive Types
`int`, `float`, `bool`, `string`, `void`, `any`

### Advanced Types
- Generic parameters: `<T>`
- Where clauses: `where T: Comparable`
- `Result<T>` — success/error sum type
- `?` operator — early return on error
- `?.` — optional chaining (short-circuit nil)

### Statements
`if/else`, `while`, `for in`, `break`, `continue`, `return`, `yield`, `match`

### Expressions
- Literals, arrays, calls, member/index access
- Assignment: `=`, `+=`, `-=`, `*=`, `/=`
- String interpolation: `f"..."`
- Anonymous `fn`, `coroutine fn`, `resume`
- `match` with struct / tuple / enum / range / guard patterns, exhaustiveness checking
- Associated functions: `TypeName::method()`

## Module System

- `set_package_root()` reads `package.toml` and adds the entry module directory to search paths
- Module bytecode is cached by mtime; cache is invalidated on source change

## Data Model

- Primitives: `nil`, `bool`, `int`, `float`, `string`
- Heap values: `Array`, functions, closures, `StructInstance`, `EnumInstance`, coroutine frames
- Host values cross the script boundary as generation-checked `HostHandleToken` values
- Core builtins: `print`, `assert`, `len`, `str`, `contains`, `starts_with`, `ends_with`, `push`, `concat`, `join`, `range`

## VM Architecture

- Register-based bytecode (`R_*` opcodes)
- Superinstruction fusion: `SI_ADD_STORE`, `SI_CMP_JUMP`, etc.
- Register allocator: live range analysis, copy propagation
- Spill fallback for >256 locals (`R_SPILL_LOAD` / `R_SPILL_STORE`, format v2)
- Two-pass semacheck: declaration hoisting + trait impl completeness
- String interning with GC root registration
- Zero AST fallback in Release builds

## GC

- Generational: nursery (young) + old generation
- Bitmap card table + write barrier for old-to-young edge tracking
- Lazy sweep, adaptive nursery sizing
- Incremental budget (`gc_step()`), full/young explicit triggers
- `start_gc_trace()` / `get_gc_trace_json()` trace export
- GC pause p50/p95/p99 counters
- Write barrier coverage: global/env writes, struct fields, arrays, closure captures, coroutine slots

## Host Handle Lifetimes

| Class | Scope | Serializable |
|---|---|---|
| `Frame` | Current call frame only | No |
| `Tick` | Current engine tick only | No |
| `Persistent` | Across runtime calls, not scene boundaries | No |
| `Stable` | Across scene boundaries | Yes |

- Invalid handle access traps in debug builds; policy-controlled fault in release
- `serialize_value()` / `call_serialized()` reject non-`Stable` handles
- Save payloads use `ZephyrSaveEnvelope` (`schema = "zephyr.save"`, `version = 2`)

## Standard Library

| Module | Contents |
|---|---|
| `std/math` | Numeric utilities |
| `std/string` | String manipulation |
| `std/collections` | Map, Set, Queue |
| `std/json` | JSON encode/decode |
| `std/io` | File read/write |
| `std/gc` | GC control and diagnostics |
| `std/profiler` | Sampling profiler, `--profile` CLI |

## Host API Surface
The host engine interacts utilizing handles, coroutine queries, GC steps, and GC tracing json pulls out of the box without complex C++ macro mappings.
