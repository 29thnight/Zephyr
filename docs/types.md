## Types

Zephyr is statically typed. Every binding has a known type at compile time.

### Primitive types

| Type | Description | Literal example |
|---|---|---|
| `int` | 64-bit signed integer | `42`, `-7`, `0` |
| `float` | 64-bit IEEE-754 double | `3.14`, `-0.5`, `1.0` |
| `bool` | Boolean | `true`, `false` |
| `string` | Immutable UTF-8 string | `"hello"` |
| `void` | No value (function return only) | — |
| `any` | Dynamic / unchecked | any value |

### nil

`nil` is the absence of a value. A variable of type `T?` (future) can hold either a `T` or `nil`.

```zephyr
let x: any = nil;
```

### int

```zephyr
let a = 10;
let b: int = -5;
print(a + b);   // 5
```

Arithmetic operators: `+`, `-`, `*`, `/`, `%`

### float

```zephyr
let pi: float = 3.14159;
let area = pi * 2.0 * 2.0;
```

Integer literals are automatically widened to `float` in mixed expressions.

### bool

```zephyr
let flag = true;
if flag { print("yes"); }
```

Logical operators: `&&`, `||`, `!`

### string

```zephyr
let greeting = "Hello";
let name = "Zephyr";
let msg = greeting + ", " + name + "!";
print(msg);   // Hello, Zephyr!
```

String interpolation with `f"..."`:

```zephyr
let x = 42;
print(f"The answer is {x}");   // The answer is 42
```

See [String](string.md) for the full string API.

### void

Used only as a function return type to indicate no value is returned.

```zephyr
fn log(msg: string) -> void {
    print(msg);
}
```

### any

`any` disables compile-time type checking for that binding. Use sparingly.

```zephyr
fn dump(val: any) -> void {
    print(val);
}
dump(42);
dump("hello");
dump(true);
```

### Type inference

When the right-hand side of a `let` binding is a literal, the type is inferred automatically:

```zephyr
let x = 10;       // int
let y = 3.14;     // float
let z = "hi";     // string
let b = false;    // bool
```

### Result\<T\>

`Result<T>` is a built-in sum type for error handling:

```zephyr
fn divide(a: int, b: int) -> Result<int> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}

match divide(10, 2) {
    Ok(v)    => print(f"Result: {v}"),
    Err(msg) => print(f"Error: {msg}"),
}
```

Use `?` to propagate errors early:

```zephyr
fn compute() -> Result<int> {
    let a = divide(10, 2)?;
    let b = divide(a, 0)?;   // returns Err immediately
    return Ok(b);
}
```
