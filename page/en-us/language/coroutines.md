# Coroutines

Coroutines are functions that can suspend their execution at a `yield` point and be resumed later from exactly where they left off. They are the primary mechanism for managing asynchronous state machines, AI logic, and sequential animations in Zephyr.

## Declaration and Creation

A coroutine is declared using the `coroutine fn` keyword. Calling a coroutine function does not execute its body immediately; instead, it returns a coroutine object that represents the suspended state of the function.

```zephyr
coroutine fn task() -> void {
    print("Step 1");
    yield;
    print("Step 2");
}

let c = task(); // Function is created but not yet executing
```

## Resumption and Suspension

Execution begins or continues only when the `resume` operator is applied to the coroutine object.

```zephyr
resume c; // Prints "Step 1" and suspends at yield
resume c; // Prints "Step 2" and returns
```

### Yielding Values

The `yield` statement can optionally include an expression. This value is returned to the caller as the result of the `resume` operation.

```zephyr
coroutine fn counter(n: int) -> int {
    let mut i = 0;
    while i < n {
        yield i;
        i += 1;
    }
}

let c = counter(3);
print(resume c); // 0
print(resume c); // 1
print(resume c); // 2
```

## Coroutine Properties

Coroutine objects expose two built-in properties to track their execution status:

| Property | Type | Description |
|---|---|---|
| `.done` | `bool` | Returns `true` if the function has finished execution (returned). |
| `.suspended` | `bool` | Returns `true` if the function is currently paused at a `yield` point. |

```zephyr
while !c.done {
    resume c;
}
```

## Nested Calls and Yield Propagation

Zephyr supports deep yielding. If a coroutine calls a regular function (`fn`), and that function (or any function further down the call stack) executes a `yield`, the entire call chain is suspended. The coroutine object maintains this deep call stack until resumed.

```zephyr
fn helper() -> void {
    print("In helper");
    yield;
}

coroutine fn main_task() -> void {
    helper();
    print("Back in main");
}
```

## Implementation Details

All coroutines are heap-resident. Each instance is backed by a `CoroutineFrame` containing:
- A snapshot of all registers (0-255).
- The current operand stack.
- The instruction pointer (PC).
- A pointer to the enclosing closure's upvalues.

When a coroutine yields, the frame is optionally compacted to reduce memory overhead. When resumed, the VM restores the register bank and continues execution from the saved instruction pointer.
