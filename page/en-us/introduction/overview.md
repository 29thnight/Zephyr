# Overview

Zephyr is a high-performance embedded scripting language for game engines, heavily inspired by Rust's syntax design.

## Core Features
- **Supported Paradigms**: Functions, Structs, Enums, Traits/Impls, Pattern Matching (`match`), Coroutines, Module Import/Export.
- **Control Flow**: Fully supports `if let`, `while let`, and range-based iterators `for i in 0..n` or `for i in 0..=n`.
- **Runtime Architecture**: Bytecode-first VM driven by Virtual Registers and Superinstructions.
- **Garbage Collection**: 4-Space generational GC with card tracking (write barriers) to strictly prevent frame spikes.
- **Coroutines**: Operates on an independent heap-reserved memory frame (`CoroutineObject`), decoupling completely from the C++ native call stack.

## Unsupported Features
Features currently excluded or unscheduled due to the strict embedding architecture:
- Generics (Type parameters) and `where` clauses.
- Macro systems.
- Rust-level Ownership/Lifetime validation (handled natively by the GC instead).
- Cross-boundary `async/await` with C++ (internally decoupled Coroutines handle logic suspensions).

<br>

<div class="custom-features-wrapper">
  <h2>Zephyr Core Architecture</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ Cache-Friendly VM</h3>
      <p>Superinstruction fusion and virtual register allocation for ultra-low latency runtime execution.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ Generational GC</h3>
      <p>4-Space heap with card tracking ensures 0-hitch frame pacing for heavy gameplay logic.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 First-class Coroutines</h3>
      <p>Seamless yield/resume flow via heap-preserved state machines, independent of the host stack.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ Strict Host Binding</h3>
      <p>Powerful 4-phase handle lifecycle to protect C++ native memory from leaks or dangling pointers.</p>
    </div>
  </div>
</div>
