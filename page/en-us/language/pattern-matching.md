# Pattern Matching

The `match` expression provides precise condition checking and data unpacking (destructuring). The first matching pattern block is executed.

> [!IMPORTANT]
> Exhaustiveness Checking
> The Zephyr compiler verifies that a `match` expression covers **every possible case** for the target enum or range. A missing case is a compile-time error. Use the wildcard `_` to explicitly handle remaining cases.

## Literals and Ranges

Match against numeric or string literals, or use the `..=` range operator:

```zephyr
let score = 85;
match score {
    90..=100 => print("A"),
    80..=89  => print("B"),
    70..=79  => print("C"),
    _        => print("F"),
}
```

## Destructuring

Extract payload data from enums, structs, or tuples directly into local variables within the `match` scope.

### Enum Patterns

```zephyr
enum Message {
    Quit,
    Move(int, int),
    Write(string),
}

let msg = Message::Move(10, 20);

match msg {
    Message::Quit        => print("quit"),
    Message::Move(x, y)  => print(f"move to {x},{y}"),
    Message::Write(text) => print(text),
}
```

### Struct Patterns

Selectively match specific fields (`x: 0`) while extracting others (`y`):

```zephyr
struct Point { x: int, y: int }
let p = Point { x: 0, y: 5 };

match p {
    Point { x: 0, y } => print(f"on y-axis at {y}"),
    Point { x, y: 0 } => print(f"on x-axis at {x}"),
    Point { x, y }    => print(f"at ({x}, {y})"),
}
```

### Tuple and Array Patterns

```zephyr
// Tuple destructuring with value matching
let pair = (true, 42);
match pair {
    (true, n)  => print(f"yes: {n}"),
    (false, n) => print(f"no: {n}"),
}

// Array pattern matching
match [1, 2, 3] {
    [1, x, y] => print(f"starts with 1, then {x} and {y}"),
    _         => print("something else"),
}
```

## Guard Conditions

Add an `if` expression after a pattern to impose an additional runtime condition:

```zephyr
let n = 7;
match n {
    x if x % 2 == 0 => print(f"{x} is even"),
    x                => print(f"{x} is odd"),
}
```

## `Result<T>` Error Handling

Because Zephyr does not use exceptions (`throw`), `match` is the idiomatic way to handle success/failure:

```zephyr
match divide(10, 0) {
    Ok(v)    => print(f"success: {v}"),
    Err(msg) => print(f"error: {msg}"),
}
```
