## Coroutine

Coroutines are functions that can suspend their execution at a `yield` point and be resumed later. They are useful for game AI, animation sequencing, and cooperative multitasking.

```zephyr
coroutine fn counter(start: int) -> int {
    mut i = start;
    while true {
        yield i;
        i += 1;
    }
}

let c = counter(0);
print(resume c);   // 0
print(resume c);   // 1
print(resume c);   // 2
print(c.done);     // false
```

### Creating a coroutine

Declare with `coroutine fn`. The function body does not execute until the first `resume`.

```zephyr
coroutine fn hello() -> void {
    print("A");
    yield;
    print("B");
    yield;
    print("C");
}

let h = hello();
resume h;   // A
resume h;   // B
resume h;   // C
print(h.done);   // true
```

### Passing values through yield

`yield <expr>` sends a value to the caller. The result of `resume` is the yielded value.

```zephyr
coroutine fn squares(n: int) -> int {
    mut i = 1;
    while i <= n {
        yield i * i;
        i += 1;
    }
}

let sq = squares(4);
while !sq.done {
    print(resume sq);
}
// 1 4 9 16
```

### Coroutine status

| Property | Description |
|---|---|
| `.done` | `true` if the coroutine has returned |
| `.suspended` | `true` if the coroutine is at a `yield` |

### Coroutines in game AI

```zephyr
coroutine fn patrol_ai(enemy: any) -> void {
    while true {
        move_to(enemy, waypoint_a);
        yield;
        wait_seconds(2.0);
        yield;
        move_to(enemy, waypoint_b);
        yield;
        wait_seconds(2.0);
        yield;
    }
}

let ai = patrol_ai(enemy_entity);

// In engine update loop:
// if !ai.done { resume ai; }
```

### Nested helper calls

Helper functions called from within an active coroutine can also yield — the entire call chain suspends together:

```zephyr
fn wait_and_log(msg: string) -> void {
    print(msg);
    yield;   // suspends the parent coroutine
}

coroutine fn sequence() -> void {
    wait_and_log("step 1");
    wait_and_log("step 2");
    wait_and_log("done");
}
```

### Implementation

Each coroutine is backed by a heap-resident `CoroutineFrame` containing a register bank snapshot, operand stack, and suspended PC. On `yield`, the frame is compacted to minimum capacity. On `resume`, execution continues from the saved PC with the original register state restored.

See [Architecture — Coroutine Model](../ARCHITECTURE.md#34-coroutine-model) for implementation details.
