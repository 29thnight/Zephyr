# Operators

Zephyr supports a standard set of operators for arithmetic, comparison, logic, and assignment.

## Arithmetic Operators

| Operator | Description | Example |
|---|---|---|
| `+` | Addition | `a + b` |
| `-` | Subtraction | `a - b` |
| `*` | Multiplication | `a * b` |
| `/` | Division | `a / b` |
| `%` | Modulo | `a % b` |
| `-` | Unary negation | `-a` |

## Comparison Operators

Comparison operators evaluate to a `bool` value.

| Operator | Description |
|---|---|
| `==` | Equality |
| `!=` | Inequality |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

## Logical Operators

| Operator | Description |
|---|---|
| `&&` | Logical AND (Short-circuits) |
| `||` | Logical OR (Short-circuits) |
| `!` | Logical NOT |

`&&` and `||` short-circuit.

## Assignment

| Operator | Equivalent |
|---|---|
| `=` | Assignment |
| `+=` | `a = a + b` |
| `-=` | `a = a - b` |
| `*=` | `a = a * b` |
| `/=` | `a = a / b` |

```zephyr
mut x = 10;
x += 5;    // 15
x *= 2;    // 30
```

## Optional chaining (`?.`)

`?.` short-circuits to `nil` when the left side is `nil` instead of raising an error.

```zephyr
let name = user?.profile?.name;
```

If any step is `nil`, the entire expression evaluates to `nil` rather than trapping.

## Error propagation (`?`)

`?` propagates `Err` from a `Result<T>` expression, returning early from the enclosing function.

```zephyr
fn load() -> Result<string> {
    let content = read_file("data.json")?;
    return Ok(content);
}
```

## Operator precedence (high → low)

| Level | Operators |
|---|---|
| 7 | `!`, unary `-` |
| 6 | `*`, `/`, `%` |
| 5 | `+`, `-` |
| 4 | `<`, `<=`, `>`, `>=` |
| 3 | `==`, `!=` |
| 2 | `&&` |
| 1 | `\|\|` |
| 0 | `=`, `+=`, `-=`, `*=`, `/=` |

Use parentheses to override precedence:

```zephyr
let result = (a + b) * c;
```
