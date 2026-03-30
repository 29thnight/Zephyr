# Syntax

Zephyr uses a brace-based syntax inspired by modern systems languages. It is case-sensitive and requires semicolons for statement termination.

## Comments

Zephyr supports both single-line and multi-line comments.

```zephyr
// This is a single-line comment

/*
   This is a
   multi-line comment
*/
```

## Keywords

The following words are reserved and cannot be used as identifiers:

| | | | | | | |
|---|---|---|---|---|---|---|
| `fn` | `let` | `mut` | `return` | `yield` | `if` | `else` |
| `while` | `for` | `in` | `break` | `continue` | `match` | `struct` |
| `enum` | `trait` | `impl` | `import` | `export` | `coroutine` | |

## Identifiers

Identifiers must start with a letter (`a-z`, `A-Z`) or an underscore (`_`). Subsequent characters can be letters, digits (`0-9`), or underscores.

```zephyr
let variable_name = 10;
let _privateCounter = 0;
let camelCase = true;
```

## Scoping

Zephyr employs **lexical (block) scoping**. A new scope is created with curly braces `{}`. Variables declared within a block are not accessible outside of it. Inner scopes can shadow variables from outer scopes.

```zephyr
let x = 10;
{
    let x = 20; // Shadows the outer x
    print(x);   // Prints 20
}
print(x);       // Prints 10
```

## Semicolons

Every statement must be terminated with a semicolon `;`. This includes variable declarations, expressions, and control flow statements inside blocks.

```zephyr
let a = 1;
let b = 2;
print(a + b);
```

## Type Annotations

While Zephyr features type inference, types can be explicitly annotated using the `:` syntax.

```zephyr
let count: int = 0;
fn add(a: int, b: int) -> int {
    return a + b;
}
```

The `any` type can be used to bypass static type checking for a specific binding.

```zephyr
let dynamic_val: any = 42;
dynamic_val = "now a string";
```

## Literals

Supported literal forms natively tokenized by Zephyr:
- **Numbers**: `100`, `3.14`
- **Boolean & Null**: `true`, `false`, `nil`
- **Arrays**: `[1, 2, 3]`
- **Format string (f-string)**: `f"hp={value}"`
