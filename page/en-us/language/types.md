# Types & Value Model

Zephyr provides a flexible dynamic runtime while applying strict structural and value validation rules to safely bridge engine-driven C++ bounds.

## Primitives
Atomic unit values within bytecode registers.

- `int`: 64-bit Signed Integer.
- `float`: Double-precision float.
- `bool`: `true` / `false` logic value.
- `string`: Garbage Collected (GC) string buffers.
- `nil`: Empty or failed value.
- `any`: Polymorphic box letting type evasion.

## Complex Types
Objects allocated into memory and fully managed by Zephyr's 4-Phase Generational Garbage Collector.

- **Array**
- **Struct / Enum Instance**
- **Function / Closure**
- **Coroutine Object** (Yieldable Heap-based state machine)

> [!NOTE] Runtime Type Checks
> To avoid fatal memory errors or pointer crashes originating from C++, the VM fiercely catches missing payloads, signature mismatches (`-> int`), and type errors instantly, then yields a catchable `RuntimeResult` internally.
