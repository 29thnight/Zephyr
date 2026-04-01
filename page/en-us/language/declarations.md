# Declarations

An overview of how to define variables and functions — the building blocks of state and logic in Zephyr.

## Variable Declaration (`let`)

Variables are immutable by default. Once assigned, they cannot be modified.

```zephyr
let score = 10;
let hp: int = 100;
```

### Mutable Variables (`mut`)

Use `mut` to declare a variable that can be reassigned:

```zephyr
mut offset = 0;  // or: let mut offset = 0;
offset += 5;
```

## Type Annotations

Append `:` followed by a type after the variable name to provide an explicit type hint:

```zephyr
let count: int = 0;
```

If you need to bypass compile-time type constraints and allow dynamic typing at runtime, use the `any` type:

```zephyr
let val: any = 42;
val = "now a string"; // allowed because type is any
```

## Scoping and Shadowing

Zephyr uses standard **lexical (block) scoping**. A new variable declared inside `{ }` can shadow an identically named variable from an outer scope:

```zephyr
let x = 10;
{
    let x = 20;   // shadows outer x
    print(x);     // 20
}
print(x);         // 10
```

## Function Declaration (`fn`)

Functions are declared with the `fn` keyword. Parameter types and the return type are required:

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}
```

> [!NOTE]
> Default parameter values and optional positional arguments are not supported. Every parameter must be supplied at the call site. Use `Result<T>` or `any` if optional data is required.

### Other Declarations

`struct`, `enum`, `trait`, generics, coroutines, and `import`/`export` are covered in their own sections.
