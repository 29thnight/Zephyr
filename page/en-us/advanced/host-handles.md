# Host Handle Policy

> [!WARNING] C++ Native Pointer Hazards (Memory Leaks & Dangling References)
> Exposing bare C++ bindings straight towards dynamically iterating script structures produces lethal systemic vulnerabilities often manifesting universally as Use-After-Free (UAF) execution violations.

Zephyr structurally integrates an impenetrable **4-Tier Generation Check Handling System** routing exact lifespan bounds seamlessly wrapping script objects gracefully across C++ interactions destroying unauthorized accesses conditionally checking structural validity transparently.

## Lifespan Hierarchy Levels

<div class="custom-features-wrapper">
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🔴 Frame</h3>
      <p>Evaluates structural failures directly capturing current Call Stack destruction. Serialization forbidden. (C++ API: `create_frame_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟠 Tick</h3>
      <p>Instantly triggers structural expiration whenever the Engine loops via `advance_tick()`. Serialization forbidden. (C++ API: `create_tick_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟡 Persistent</h3>
      <p>Executes mass clearances upon transitions updating references navigating `advance_scene()`. Serialization forbidden. (C++ API: `create_persistent_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟢 Stable</h3>
      <p>Relegates explicit invalidation structurally requesting manual `invalidate_host_handle()` logic. <b>Only handle compatible with Serialization!</b></p>
    </div>
  </div>
</div>

## Synchronous Update Pipelines

Engine Core integrators orchestrate loop evaluations directly dictating boundary limitations manually executing procedural generation skips cleanly.

```cpp
// Clears internal Persistent allocations directly triggering state boundaries
rt.advance_scene();

// Relegates dynamic Tick generations gracefully executing rendering increments
rt.advance_tick();
```

> [!NOTE] Virtual Access Mechanics
> When native script limits fetch (query) Invalidated bounds structurally mapping C++ hosts natively, **Debug Configurations execute immediate structural Traps analyzing exception traces safely**, whilst evaluating **Release Constructs gracefully entering standard execution Fallback Policies isolating fatal game closures elegantly**.

## Envelope Synchronization (Save / Load)

Encoding raw structural tree bindings exporting runtime limits navigating state logic cleanly (Save files natively):

```cpp
std::string json = rt.serialize_value(my_script_root_value);
```

This procedure rigorously rejects every `Persistent` and `Tick` instance filtering explicit native references strictly evaluating `Stable` handles preserving true JSON isolation effectively avoiding dummy pointer encodings actively blocking invalid game file generations smoothly.

## RAII Reference Mapping (`ZephyrHandle<T>`)

Transmitting further than primitive structural traps natively encapsulating C++ object definitions tightly, developers actively isolate instances preserving `ZephyrValue` boundaries safely spanning manual `Tick` bounds explicitly invoking robust wrappers natively bypassing generational cycles cleanly via **`ZephyrHandle<T>`**.

This logic evaluates native destruction conditions inherently dispatching `vm.pin_value()` avoiding garbage sweeps safely releasing handles sequentially wrapping structural closures explicitly running `vm.unpin_value()` actively resolving references elegantly.

```cpp
// Anchors specific Zephyr variables protecting structural allocations elegantly.
// Spanning native RAII scopes completely averts manual destruction checkpoints perfectly!
auto h = ZephyrHandle<Player>(vm, vm.make_host_object(player_ptr));

// Evaluates embedded underlying states parsing pointers fluidly
if (h) {
    h.get()->damage(10);
}
```
