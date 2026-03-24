# Zephyr

Zephyr is a Rust-flavored, GC-managed scripting language aimed at game embedding. This repository ships:

- `Zephyr.sln` Visual Studio solution
- `ZephyrRuntime` static library
- `zephyr_cli` command line tool with `run`, `check`, `repl`, `stats`, `dump-bytecode`, and `bench`
- `zephyr_bench` benchmark harness that emits JSON reports
- `zephyr_engine_sample` host integration sample
- `zephyr_tests` lightweight test executable

## Build

Open `Zephyr.sln` in Visual Studio and build the solution, or use `MSBuild.exe Zephyr.sln /p:Configuration=Debug /p:Platform=x64`.

## Commands

- `zephyr run examples/engine_demo.zph`
- `zephyr check examples/import_main.zph`
- `zephyr repl`
- `zephyr stats examples/import_main.zph`
- `zephyr dump-bytecode examples/import_main.zph main`
- `zephyr bench bench/results/latest.json --strict`
- `zephyr bench bench/results/latest.json --baseline bench/results/v1_baseline.json --strict`

## Language Snapshot

- Rust-inspired syntax: `fn`, `let`, `mut`, `struct`, `enum`, `match`
- Control flow additions: `break`, `continue`, `+=`, `-=`, `*=`, `/=`
- First-cut coroutine surface: `coroutine fn { ... }`, `yield`, `resume expr`, and `.done`
- Dynamic runtime with optional runtime-checked type annotations
- Relative-path `import` and host-module `import`
- GC-managed arrays, strings, closures, structs, and enums
- Generation-checked host handles with lifetime policies for `Frame`, `Tick`, `Persistent`, and `Stable`
- Core helpers such as `str`, `contains`, `starts_with`, `ends_with`, `push`, `concat`, `join`, and `range`

## Host Handle Policy

- Native host instances remain owned by the embedding app; Zephyr GC never frees engine objects or GPU/OS resources
- Script-visible host references are generation-checked handles, not raw pointers
- `Frame` / `Tick` handles are transient and rejected for global storage, field storage, and closure capture
- `Persistent` handles are long-lived but scene-local; `Stable` handles survive scene changes and are the only serialization-eligible class
- Reusing the same native host class + instance preserves script equality and handle identity inside the VM unless the host invalidates the handle
- If the host invalidates a handle, debug builds trap immediately and release builds can downgrade to a recoverable runtime fault according to policy
- `ZephyrVM::serialize_value()` and `ZephyrVM::call_serialized()` reject non-`Stable` handles so save/load boundaries cannot accidentally capture weak scene-local references
- Save/export output uses a versioned `ZephyrSaveEnvelope` record with `schema = "zephyr.save"`, `version = 1`, and a tagged `payload` tree of `ZephyrSaveNode` records
- Coroutine yield storage uses the same handle policy checks, so `Frame` / `Tick` handles cannot cross a yield boundary

## Runtime Integration

- `ZephyrVM::gc_step()` performs budgeted incremental GC work
- `ZephyrVM::set_gc_stress()` enables debug-style safepoint GC stepping inside bytecode execution
- `ZephyrVM::advance_frame()` / `advance_tick()` roll the engine epochs and optionally consume GC budget
- `ZephyrVM::advance_scene()` performs a scene epoch transition and allows a full collection
- `ZephyrVM::invalidate_host_handle()` lets the host proactively retire a handle
- `ZephyrVM::gc_verify_full()` forces a full collection and then validates heap/root/handle invariants for debug or loading-screen verification passes
- `ZephyrVM::serialize_value()` validates a public value tree for save/export safety and emits the versioned `ZephyrSaveEnvelope` schema
- `ZephyrVM::deserialize_value()` accepts the current envelope schema and older legacy plain public-value payloads for backward compatibility
- `ZephyrVM::call_serialized()` calls a script function and returns only serialization-safe data wrapped in the same versioned envelope
- `ZephyrVM::capture_callback()` / `release_callback()` let the host retain script callbacks without keeping raw VM pointers
- `ZephyrVM::spawn_coroutine()` / `resume()` / `cancel()` / `query_coroutine()` expose retained coroutine handles for host-driven scheduling
- `ZephyrVM::collect_young()` and `ZephyrVM::gc_verify_young()` expose the nursery-focused v2 collection path for testing and diagnostics
- Minor collection now traces young objects from the root set plus an object-granularity remembered set of old owners that directly reference young objects; root/module environments are scanned directly as roots instead of being duplicated into the remembered set
- Old local environments, large old arrays, old struct/enum instances, and suspended coroutine frames now keep value-card metadata so minor collection can rescan only dirty slices instead of the full container
- Minor remembered-set compaction now prunes fully card-tracked owners from card state alone instead of re-walking the whole object graph
- `ZephyrVM::runtime_stats()` exposes GC phase, allocation, dirty-root/dirty-object queue depth, remembered-owner/card counts, barrier, invalid-handle, local/global binding cache hit rates, epoch, GC-stress safepoint counts, coroutine depth, and coroutine frame slot usage counters for profiling
- `ZephyrVM::debug_dump_coroutines()` prints the current heap-resident coroutine set with status plus per-frame `ip/stack/local/scope` details
- `ZephyrVM::dump_bytecode()` and `zephyr bench` expose bytecode histograms and repeatable JSON benchmark baselines for v2 perf work
- `zephyr_bench` and `zephyr bench` both accept `--baseline` and `--strict`; strict mode returns a failing exit code when any acceptance gate regresses
- If `bench/results/v1_baseline.json` exists, both runners auto-discover it and enable the default acceptance gate set even when `--baseline` is omitted
- Suspended coroutines now compact their frame-local `stack/local/scope` buffers on `yield`, and the compaction totals are visible in runtime stats
- Coroutine diagnostics also track per-coroutine `resume` / `yield` counts and cumulative bytecode step cost so expensive resumptions are easier to spot
- Local-slot bytecode execution now uses slot-to-binding caches, nested function/coroutine literals are precompiled into parent chunks, and captured locals flow through GC-managed upvalue cells with `LoadUpvalue` / `StoreUpvalue` instead of name lookup on the hot path
- Pure bytecode closures/coroutines now keep only the module/root closure path plus injected upvalue-cell bindings at runtime; debug builds still allow AST-fallback chunks to retain the full lexical closure chain, while release builds reject those chunks outright
- `resume` expressions now execute through a native bytecode opcode too, shrinking one more common source of AST fallback in retained gameplay closures
- Compound `obj.field += value` and `arr[index] += value` assignments now lower to native bytecode using temporary locals instead of falling back to the AST interpreter
- Unresolved global/module names now compile to per-chunk slots, and both sync/coroutine bytecode paths cache a module/root-scoped binding with version checks so repeated global lookups avoid walking nested local scope chains
- Benchmark reports now summarize pass/fail/skipped gate counts, compare hot-loop / host-handle / coroutine costs against an optional v1 baseline, and always keep a native-bytecode/no-AST-fallback gate in the report
- Incremental write barriers now dedupe repeated dirty root/object logging with a dirty-queue bit, while minor-GC remembered owners use a separate durable bit so old-to-young edges survive across nursery collections
- GC stress mode can force tiny incremental steps at bytecode safepoints, which is useful for flushing out missing roots or barrier bugs during debug runs

## Coroutine Notes

- The current coroutine implementation is now lazy and keeps its VM frame on the heap across `yield` / `resume`
- `resume` runs until the next `yield` or final `return`, preserving local slots, operand stack, and scope environments between resumes
- Coroutine runtime state now lives in a single heap frame-stack model instead of mirrored active/saved frame copies
- Nested script function calls now suspend through the active coroutine across multiple helper layers, so deeper call chains can `yield` and later resume through the same frame stack
- `.done` becomes `true` after the final result has been consumed, and `.suspended` reports whether the coroutine is currently parked at a yield point
- `Frame` / `Tick` handles are still rejected across yield boundaries

## Example Scripts

- `examples/state_machine.zph` shows state transitions with enums and `match`
- `examples/event_handling.zph` shows name-based event handler registration for an embedded host
- `examples/simple_ai.zph` shows a small AI decision tree with `struct` + `enum`
