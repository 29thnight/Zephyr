# Zephyr Mini Spec

## Goals

- Rust-like surface syntax without borrow checking or lifetime annotations
- Lua-like immediacy for gameplay scripts
- Embeddable C++ runtime with explicit host bindings
- Bytecode-first execution with debug-only AST fallback retained for backend bring-up; release builds reject AST-fallback bytecode and stay bytecode-only on supported surface syntax

## Supported v1 Surface

- Declarations: `fn`, `let`, `struct`, `enum`, `import`, `export`
- Statements: block, `if`, `while`, `for in`, `break`, `continue`, `return`, `yield`
- Expressions: literals, arrays, calls, member/index access, assignment, `+=`, `-=`, `*=`, `/=`, anonymous `fn`, `coroutine fn`, `resume`, `match`
- Runtime contracts: parameter, return, variable, struct field, and enum payload checks

## Excluded in v1

- Traits, generics, macros, ownership, async, package manager
- Deep destructuring and `match` guards
- Automatic reflection-based host exposure

## Imports

- `import "foo.zph";` brings exported bindings into local scope
- `import "foo.zph" as foo;` binds exported namespace to `foo`
- `import "engine";` loads a host module registered by the embedding app

## Data Model

- Primitive values: `nil`, `Bool`, `Int`, `Float`, `String`
- Heap values: `Array`, script/native functions, `StructInstance`, `EnumInstance`
- Host values cross the script boundary as generation-checked `HostHandleToken` values instead of GC-owned wrapper objects
- Core helpers include `print`, `assert`, `len`, `str`, `contains`, `starts_with`, `ends_with`, `push`, `concat`, `join`, and `range`
- GC: non-moving incremental mark-sweep with frame/tick-budgeted `gc_step()`, plus full collection on explicit `collect_garbage()` / scene transitions
- Root set includes VM stack, call environments, active and suspended coroutine stubs, interned strings, native callback registry, pinned debug objects, and loaded module environments
- Write barrier coverage includes global/environment writes, struct field writes, array writes, closure capture, and coroutine slot writes
- Marking-time dirty root/object tracking now uses a dedicated dirty-queue bit so repeated writes to the same owner do not keep growing the dirty queues
- Minor collection now uses a durable object-granularity remembered set for old-to-young heap edges; root/module environments are scanned as roots instead of being duplicated into that set
- Old local environments, large old arrays, old struct/enum instances, and suspended coroutine frames additionally track value cards so minor GC can rescan only dirty card ranges instead of whole containers
- Fully card-tracked old owners now leave the remembered set through a card-only fast path during minor compaction instead of requiring a full object rescan
- Debug-style GC stress can be enabled at bytecode safepoints to drive tiny incremental steps during script execution and surface root/barrier bugs earlier

## Host Handle Lifetimes

- `Frame` and `Tick` handles are stack-local only and cannot be stored in module globals, struct fields, arrays, or closure captures
- `Frame` and `Tick` handles are also rejected for coroutine yield storage and any future suspended coroutine frame capture
- `Persistent` handles can stay in script memory across runtime calls, but cannot cross scene/save boundaries
- `Stable` handles may cross scene boundaries and are the only handle class eligible for serialization-facing flows
- Invalid handle access traps in debug builds and becomes a policy-controlled runtime fault in release builds
- Strong residency is opt-in and intended for asset-like references only; generic entity/component handles remain weak by default
- `serialize_value()` and `call_serialized()` enforce the same rule at the API boundary by rejecting non-`Stable` handles during save/export flows
- Save/export payloads use a versioned `ZephyrSaveEnvelope` record with `schema = "zephyr.save"`, `version = 1`, and a tagged `payload` tree made of `ZephyrSaveNode` records
- `deserialize_value()` accepts that envelope and still accepts legacy plain public-value trees for backward compatibility during migration

## Execution

- Modules are compiled to bytecode chunks for one-time initialization as well as script function bodies
- Script function bodies are compiled to a stack-based bytecode chunk on creation
- Parameters and lexical `let` bindings use compile-time resolved local slots, and the hot bytecode path now caches slot-to-binding resolution instead of repeatedly refreshing locals from environments
- Unresolved global/module identifiers also compile to per-chunk name slots, and runtime frames cache the resolved module/root binding with environment-version invalidation so repeated global access avoids repeated nested-scope parent-chain lookup
- Nested function and coroutine literals are precompiled into parent bytecode chunks, and captured outer locals compile down to `LoadUpvalue` / `StoreUpvalue` slots instead of name-based lookup on the hot path
- Closure capture still promotes local bindings to GC-managed upvalue cells so nested and returned closures share mutable captured state across GC and outer-scope teardown
- Bytecode-only closures and coroutines slim their retained closure chain to the module/root path and materialize captured upvalue bindings into the runtime frame environment; debug-only chunks with `EvalAstExpr` / `ExecAstStmt` keep the full lexical chain, while release builds reject them
- `resume` expressions now compile to a native bytecode opcode instead of forcing AST fallback
- Compound member/index assignments also compile to native bytecode by storing RHS/object/index temporaries in a short-lived scope before the final store
- Top-level `import`, `let`, `fn`, `struct`, `enum`, and control flow now run through the bytecode VM during module load
- Core control flow such as `if`, `while`, `for in`, `break`, and `continue` now runs natively in the bytecode VM
- Struct literals, enum constructors, and `match` dispatch now also execute through native bytecode opcodes
- Unsupported nodes now fail fast during bytecode execution; only debug builds keep AST fallback opcodes available for backend bring-up or hand-authored diagnostic chunks
- The runtime exposes `gc_step()`, `collect_young()`, `set_gc_stress()`, `advance_frame()`, `advance_tick()`, `advance_scene()`, `invalidate_host_handle()`, `gc_verify_young()`, `gc_verify_full()`, `runtime_stats()`, `debug_dump_coroutines()`, and `dump_bytecode()` for engine integration and diagnostics, including local/global binding cache counters and coroutine frame slot usage
- `coroutine fn` / `yield` / `resume` now use a lazy suspend/resume model backed by a heap-resident coroutine VM frame
- Coroutine state preserves local slots, operand stack, and lexical scope environments across `yield` boundaries and GC cycles
- Coroutine execution state is stored as a single heap frame stack, so nested helper calls resume without a separate active-frame mirror
- Script helper functions called from inside an active coroutine can also suspend and resume through the same coroutine call chain, including deeper nested helper stacks
- Coroutine objects expose `.done` and `.suspended` for simple polling-style gameplay flows
- Suspended coroutine frames compact their `stack/local/scope` buffer capacity on `yield`, and that compaction is surfaced through runtime diagnostics
- Runtime diagnostics also expose coroutine `resume` / `yield` counts and cumulative bytecode step cost for profiling long-lived scripted workflows
- `gc_verify_full()` performs a full collection and then verifies heap reachability, root membership, remembered-set coherence, and stable-handle table consistency
- The public host API now includes retained script callback handles, retained coroutine handles, and JSON benchmark tooling through `zephyr_bench`
- Benchmark reports expose pass/fail/skipped gate counts, compare hot-loop / host-handle / coroutine costs against an optional v1 baseline, and support a strict non-zero-exit mode for regression gating
- The benchmark runners auto-discover `bench/results/v1_baseline.json` when present so the default repository workflow exercises the acceptance gates without extra CLI flags
- The sample set now includes state-machine, event-handling, and simple-AI oriented scripts for v1 validation
