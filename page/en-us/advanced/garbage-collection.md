# Garbage Collection Architecture

> [!IMPORTANT] Frame-drop (hitch) prevention for game workloads
> A classic Stop-The-World GC full sweep blocks the main thread for an extended period, causing visible frame stutters in a game loop.

Zephyr's GC is purpose-built for game engines: a **generational** heap layout combined with an **incremental** pipeline that limits work to a configurable per-frame time budget.

<div class="custom-features-wrapper">
  <h2>Generational Heap Layout</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🌱 Nursery (Young)</h3>
      <p>All new script objects are allocated here. Frequent, lightweight Young GC cycles quickly reclaim short-lived dead references.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🌳 Old Generation</h3>
      <p>Objects that survive enough GC cycles are promoted (tenured) here for long-term residence. Full sweeps of this region are rare.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🔗 Write Barrier & Card Table</h3>
      <p>A bitmap card table tracks Old→Young pointer mutations, allowing the GC to rescan only dirty regions instead of the entire old-gen.</p>
    </div>
  </div>
</div>

## Host (C++) GC Manual Control

Provide a per-frame microsecond budget to advance GC work incrementally:

```cpp
rt.advance_tick();

// Allow the GC at most 1,000 µs (1 ms) this frame
rt.gc_step(1000);
```

During loading screens or scene transitions — when the main loop is idle — trigger a full sweep to reclaim all garbage at once:

```cpp
rt.advance_scene();

// Full Stop-The-World sweep
rt.collect_garbage();
// Or a lighter Young-only collection:
// rt.collect_young();
```

## Built-in Diagnostics

Use `rt.start_gc_trace()` and `rt.get_gc_trace_json()` to capture a profiling trace. The JSON output includes timestamped allocation events, pause durations (p50/p95/p99 histogram), and promotion counts — compatible with Chrome's tracing format for visualization.
