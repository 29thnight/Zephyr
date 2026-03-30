# Type System

Zephyr is a statically typed language. Every binding has a known type at compile time, ensuring memory safety and execution efficiency.

## Primitive Types

| Type | Description |
|---|---|
| `int` | 64-bit signed integer. |
| `float` | 64-bit IEEE-754 double-precision floating point. |
| `bool` | Boolean value (`true` or `false`). |
| `string` | Immutable UTF-8 encoded string. |
| `void` | Represents the absence of a value; used only for function return types. |
| `any` | Disables compile-time type checking for a specific binding. |

## Type Inference

The compiler automatically infers the type of a variable when it is initialized with a literal.

```zephyr
let x = 10;      // Infer int
let y = 3.14;    // Infer float
let z = "hello"; // Infer string
```

## `nil`

`nil` represents the absence of a value. It can be assigned to `any` types or used in future null-safe types (`T?`).

```zephyr
let x: any = nil;
```

## Result<T> and Error Propagation

`Result<T>` is a built-in enum used to handle operations that can fail.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}
```

### The `?` Operator

The `?` operator can be appended to an expression that returns a `Result<T>`. If the result is `Ok(v)`, it evaluates to `v`. If the result is `Err(msg)`, the current function immediately returns that `Err`.

```zephyr
fn attempt_operation() -> Result<int> {
    let value = perform_risky_task()?; // Returns Err if task fails
    return Ok(value + 1);
}
```

## Mixed Expressions

Integer literals are automatically widened to `float` when used in expressions containing other float values.

```zephyr
let a = 10;
let b = 2.5;
let c = a + b; // Result is 12.5 (float)
```

## Complex Types

Objects allocated into memory and fully managed by Zephyr's 4-Phase Generational Garbage Collector.

- **Array**
- **Struct / Enum Instance**
- **Function / Closure**
> [!NOTE] Runtime Type Checks
> To avoid fatal memory errors or pointer crashes originating from C++, the VM fiercely catches missing payloads, signature mismatches (`-> int`), and type errors instantly, then yields a catchable `RuntimeResult` internally.
