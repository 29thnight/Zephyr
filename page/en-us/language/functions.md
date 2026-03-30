# Functions and Closures

Functions are the fundamental units of code execution in Zephyr. Closures are anonymous functions that capture their enclosing lexical environment.

## Function Declarations (fn)

Global functions are declared using the `fn` keyword. Parameters and return types must be explicitly annotated.

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}
```

## Anonymous Closures

Closures are function literals that can be assigned to variables or passed as arguments.

```zephyr
let multiply = fn(a: int, b: int) -> int {
    return a * b;
};

let result = multiply(10, 5); // 50
```

## Capturing Variables

A closure captures variables from its enclosing scope by reference. 

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

## Capturing Mutable State

A `mut` binding captured by multiple closures is shared among them:

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

## Closures as Callbacks

Closures are commonly passed to functions as callbacks to control inverted logic:

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
