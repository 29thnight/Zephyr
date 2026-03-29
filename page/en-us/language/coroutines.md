# Coroutines

The core asynchronous state-machine solution in game logic mapping, letting numerous AI entities await or pause ticks without overheads.

## `coroutine fn` / `yield` / `resume`

Preserved independently on the Garbage Collector's dynamic heap rather than relying on C++ native call stacks (`State Machines`), ensuring completely jitter-free control inversion flows.

```zephyr
// Initializing State Machines utilizing coroutine hooks
coroutine fn worker(limit: int) -> int {
  let mut i: int = 0;
  while i < limit {
    yield i;      // Reverts execution thread immediately back to the caller
    i = i + 1;
  }
  return i;       // Concludes the pipe (marked as Done)
}

fn run() -> int {
  // worker(n) halts execution upfront, yielding a Coroutine Object token
  let c = worker(3);
  
  let a = resume c;  // steps onto the 1st yield -> 0
  let b = resume c;  // steps onto the 2nd yield -> 1
  
  return a + b;      // Evaluates to 1
}
```
Further expansions on the observer pattern properties (`c.done`, `c.suspended`) are heavily requested and expected to land shortly.
