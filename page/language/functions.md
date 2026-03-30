# Functions

Zephyr treats functions as first-class citizens (`fn`), meaning they can be assigned internally, yielded, or transferred sequentially as callback primitives over to Native C++ engine endpoints.

## Defining a Function

Every function binds an argument map alongside an explicit trailing `->` Return Type notation representing the expected output. Default types natively infer `void` if unspecified.

```zephyr
fn add(x: int, y: int) -> int {
    return x + y;
}

print(add(5, 10)); // 15
```

## Closures (Anonymous Functions)

Zephyr seamlessly captures inner-block environmental values using the `|arg1, arg2| { ... }` token mappings commonly identified as Closures (Upvalues). They behave dynamically and persist enclosed scope lifespans across engine iterations.

```zephyr
fn create_counter() -> any {
    mut count = 0;
    
    // Captures the local 'count' binding directly into upvalue heap
    return || -> int {
        count += 1;
        return count;
    };
}

let ticker = create_counter();
print(ticker()); // 1
print(ticker()); // 2
```

## First-Class Handling Capabilities

Passing encapsulated logic logic over properties ensures flexible callback setups. For example, feeding anonymous routines over to a collection iterator natively:

```zephyr
fn run_twice(callback: any) -> void {
    callback();
    callback();
}

run_twice(|| { print("Executed!"); });
```

### Recursion Checks

To shield the execution environment from hard crashes, AST verification monitors recursion depths. If consecutive callbacks dive too deep recursively, Zephyr safely emits an internal `Stack Overflow` exception gracefully bypassing underlying host heap corruptions.
