# Zephyr Scripting Language

Zephyr is a fast, lightweight, and thoroughly modern script language meticulously designed from the ground up to be embedded directly into C++ applications—primarily video game engines.

Its core philosophy revolves around mitigating the pain points of general-purpose scripting languages when bound to high-performance C++ environments. Zephyr achieves this by adopting a **Zero-Cost C++ Object Reflection** and **Heap Frame Suspensions** approach.

<div class="custom-features-wrapper">
  <h2>Zephyr Core Architecture</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ Blazing Fast VM</h3>
      <p>A highly specialized Register-Based Virtual Machine. Skips AST-walking and interprets optimized bytecode directly, reducing memory cache misses.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ Advanced GC for Games</h3>
      <p>Runs incrementally without massive Stop-The-World spikes. Features a 4-tier Generational layout with nursery and card table algorithms.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 First-class Coroutines</h3>
      <p>Write asynchronous logic sequentially using `coroutine fn` and `yield`. The state leverages heap allocation, bypassing native OS thread overhead entirely.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ Safe Host Checking</h3>
      <p>Employs a 4-step generational `Handle` pipeline (Frame, Tick, Persistent, Stable) to immediately sandbox invalidated native objects.</p>
    </div>
  </div>
</div>

## Language Architecture

Instead of compiling purely ahead-of-time (AOT), Zephyr provides a Just-In-Time compiled (JIT) bytecode pipeline to favor hot-restorable iteration.

1. **Parser & Lexer**: Scans the incoming `.zph` source code into an Abstract Syntax Tree (AST).
2. **Sema (Semantic Analysis)**: Analyzes typings and tracks variable scoping, throwing exceptions upon identifying shadowed or undefined constraints.
3. **Compiler**: Compiles the AST down to highly compact register-based opcodes, packaging it alongside a static metadata table (Constants Pool).
4. **VM Runtime**: Executes bytecode at native speeds. Through C++ function pointers (Delegates), native bindings are accessed seamlessly.

```zephyr
// Example: Basic function and Rust-style return mechanics
fn calculate_damage(base: float, multiplier: float) -> float {
    return base * multiplier;
}
```
