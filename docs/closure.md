## Closure

A closure is an anonymous function that captures variables from its enclosing scope.

```zephyr
fn make_adder(n: int) -> fn(int) -> int {
    return fn(x: int) -> int {
        return x + n;   // captures n
    };
}

let add5 = make_adder(5);
print(add5(3));    // 8
print(add5(10));   // 15
```

### Capturing mutable state

Closures capture by reference. A `mut` binding captured by multiple closures is shared:

```zephyr
fn make_counter() -> fn() -> int {
    mut count = 0;
    return fn() -> int {
        count += 1;
        return count;
    };
}

let counter = make_counter();
print(counter());   // 1
print(counter());   // 2
print(counter());   // 3
```

### Closures as callbacks

```zephyr
fn repeat(n: int, f: fn(int) -> void) -> void {
    mut i = 0;
    while i < n {
        f(i);
        i += 1;
    }
}

repeat(3, fn(i: int) -> void {
    print(f"step {i}");
});
// step 0
// step 1
// step 2
```

### Implementation notes

Captured locals are promoted to GC-managed **upvalue cells** so closures can outlive their defining scope. The upvalue chain is compacted to module/root path only in release builds; debug builds retain the full lexical chain for `EvalAstExpr` nodes.

Bytecode disassembly shows capture instructions:

```
R_LOAD_UPVALUE  r0, slot=0   ; capture 'n'
R_LOAD_LOCAL    r1, slot=1   ; load 'x'
R_ADD           r2, r0, r1
R_RETURN        r2
```
