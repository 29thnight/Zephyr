# Coroutines

Zephyr features first-class `coroutine` capabilities. Native scripts can instantly pause execution mid-flight utilizing `yield` breakpoints preserving their internal stack frames into lightweight compacted Heap data buffers. This replaces threading bottlenecks generally tied to timeline events like Animation Sequencing or robust AI State Machine maneuvers natively.

## Instantiating and Executing

Declare your routine boundaries prefaced utilizing the `coroutine fn` keyword.
When querying a coroutine internally, understand that execution halts precisely prior to parsing the first instruction sequence, resuming entirely manually via subsequent `resume` triggers jumping towards underlying `yield` checkpoints.

```zephyr
coroutine fn hello() -> void {
    print("A");
    yield;
    print("B");
}

let h = hello();
resume h;   // Evaluates "A" and suspends.
resume h;   // Evaluates "B" and concludes execution logic.
print(h.done); // returns true.
```

## Reviewing Iteration Status

Coroutine properties (CoroutineFrame instances) contain intuitive properties verifying loop termination checks seamlessly native to execution runtimes:
- `.done` : A bool evaluating `true` the moment internal functions `return` fully closing the routine payload.
- `.suspended` : A bool evaluating `true` implying processing sequences currently reside actively nested on an intermediate `yield` block.

## Yielding Values Bidirectionally

Return values across paused timelines safely transmitting payloads utilizing `yield <expr>`.

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
    print(resume sq); // 1, 4, 9, 16 
}
```

## Nested Helper Sub-Layer Yields

Suspend entire branching logic sequentially across standard child helper method invokes nested internally. Any internal component calling a generic `yield` automatically freezes their corresponding parent coroutine hierarchy entirely.

```zephyr
fn wait_and_log(msg: string) -> void {
    print(msg);
    yield;   // Cascades freeze operations backwards cleanly!
}

coroutine fn sequence() -> void {
    wait_and_log("step 1");
    wait_and_log("step 2");
}
```

## Implementation Architecture

Zephyr sheds bloated traditional OS Thread Context Switches by swapping pointer structures utilizing `CoroutineFrame` variants residing safely across GC boundaries alongside native VM Registers mapping their underlying logic execution.

The Native Environment Engine securely parses C++ host loop routines ticking `VM.resume()` mapping 60hz intervals dynamically avoiding overhead delays entirely!
