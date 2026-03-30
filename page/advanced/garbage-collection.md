# Garbage Collection Architecture

> [!IMPORTANT] Frame Spikes and Jitter Reduction
> Implementing generic Stop-The-World Full Execution Pauses scales horribly across demanding Game Engine limits frequently stalling runtime ticks completely creating horrific visual jitters.

Zephyr bypasses traditional monolithic bounds executing highly structured **Generational** constraints heavily distributed utilizing strictly segmented **Incremental** Garbage logic tracking.

<div class="custom-features-wrapper">
  <h2>Spatial Generational Separation</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🌱 Nursery (Young)</h3>
      <p>Every script object instantly anchors memory allocations heavily towards Nursery heaps initially. Optimized for lightweight `Young GC` operations.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🌳 Old Generation</h3>
      <p>Upon surviving continuous iteration cycles gracefully, variables transition permanently here. Scanning this space is resource-intensive.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🔗 Write Barriers & Maps</h3>
      <p>Underlying tracking mechanisms monitoring Old-to-Young referential mutations dynamically. Mitigates excessive scanning allowing ultra-fast `Young GC` clearances safely.</p>
    </div>
  </div>
</div>

## C++ Engine Control Systems

Instead of freezing execution randomly, Host instances statically deliver exact microsecond timing sequences (µs Budgets) controlling precisely the max processing width allowing structural increments safely.

```cpp
rt.advance_tick();

// Directs the Garabge Collector towards sweeping utilizing an exact 1ms cap.
rt.gc_step(1000); 
```

Whenever Scene Level bounds load entirely masking visual updates beneath screen boundaries safely, executing full-sweeps aggressively reallocates scattered structural caches forcefully avoiding internal fragmented arrays inherently.

```cpp
rt.advance_scene();

// Aggressive Full Stop-The-World Evaluation
rt.collect_garbage(); 
// Execute targeted light iteration cycles solely.
// rt.collect_young();
```

## Trace Export Analytics

Housed completely via the AST compiler boundaries, you can manually target `rt.start_gc_trace()` intercepting raw GC Hitches natively structuring fully compliant Chrome Trace profiling formats outputting JSON (p50/p95/p99 histograms) pinpointing absolute worst-case frame drops transparently.
