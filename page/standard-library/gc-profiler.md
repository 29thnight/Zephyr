# GC & Profiler

System-level modules built to monitor script performance states, trace bottlenecks in in-game debug overlays, and manually govern the Garbage Collection mechanism when under heavy memory pressure.

## Garbage Collector Control (`std/gc`)

While most incremental GC cycle management runs implicitly within the host Engine Loop (C++), the script can trigger forced collections manually, which is highly useful during loading screens or interactive cutscenes where memory offloading is urgent.

```zephyr
import "std/gc";

gc.collect_young();    // Run a lightweight Nursery (Young generation) collection
gc.collect();          // Force a full Stop-The-World collection of the entire heap
gc.step(500);          // Step the incremental GC forward within a strict budget (e.g. 500 µs)
let s = gc.stats();    // Returns a Map filled with memory allocations and GC pause history
```

## Profiling Tracker (`std/profiler`)

Directly from the script, you can toggle a sampling execution profiler to scrutinize where CPU spikes are originating in the game engine. Once complete, it writes out a JSON formatted profiling report.

```zephyr
import "std/profiler";

profiler.start(); // Begin tracking

// ... Heavy loop rendering or intense network parsing logic ...

profiler.stop();   // End tracking
profiler.report(); // Dump trace output directly to stdout
```

> If you require global tracking without injecting script code, simply append the `--profile` argument to the CLI. This extracts profile data (`zephyr_profile.json`) covering the absolute program lifecycle.

```bash
zephyr run --profile mygame.zph
```
