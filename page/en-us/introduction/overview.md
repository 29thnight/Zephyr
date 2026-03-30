# Overview

Zephyr is a high-performance embedded scripting language for game engines, heavily inspired by Rust's syntax design.

## Core Features
- **Supported Paradigms**: Functions, Structs, Enums, Traits/Impls, Pattern Matching (`match`), Generics, Coroutines, Module Import/Export.
- **Control Flow**: Supports `if let`, `while let`, and range-based iterators (`0..n`, `0..=n`).
- **Runtime Architecture**: Register-based VM with superinstruction fusion.
- **Garbage Collection**: 4-Space generational collector with card tracking and write barriers.
- **Coroutines**: Independent heap-reserved memory frames (`CoroutineObject`), decoupled from the C++ native call stack.

## Constraints
- No macro system.
- No Rust-level ownership/lifetime validation (memory managed via generational GC).
- Cross-boundary `async/await` with C++ is handled via coroutine suspension.

<div class="custom-features-wrapper">
  <h2>Technical Specifications</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ VM Execution</h3>
      <p>Superinstruction fusion and virtual register allocation for optimized instruction dispatch.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ Memory Management</h3>
      <p>4-Space generational GC with write barriers to ensure deterministic frame timing.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 State Management</h3>
      <p>Heap-preserved coroutine frames for complex asynchronous logic without stack overhead.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ Host Integration</h3>
      <p>Lifecycle-managed handles for safe C++ native memory bridging and pointer protection.</p>
    </div>
  </div>
</div>
