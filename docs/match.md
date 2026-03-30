## Pattern Matching

`match` dispatches on a value by testing a series of patterns. The first matching arm is executed. All possible cases must be covered (exhaustiveness check) or a wildcard `_` must be present.

### Literal patterns

```zephyr
let x = 2;
match x {
    1 => print("one"),
    2 => print("two"),
    3 => print("three"),
    _ => print("other"),
}
// two
```

### Range patterns

```zephyr
let score = 85;
match score {
    90..=100 => print("A"),
    80..=89  => print("B"),
    70..=79  => print("C"),
    _        => print("F"),
}
// B
```

### Enum patterns

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
// move to 10,20
```

### Struct patterns

```zephyr
struct Point { x: int, y: int }
let p = Point { x: 0, y: 5 };

match p {
    Point { x: 0, y } => print(f"on y-axis at {y}"),
    Point { x, y: 0 } => print(f"on x-axis at {x}"),
    Point { x, y }    => print(f"at ({x}, {y})"),
}
// on y-axis at 5
```

### Tuple patterns

```zephyr
let pair = (true, 42);
match pair {
    (true, n)  => print(f"yes: {n}"),
    (false, n) => print(f"no: {n}"),
}
// yes: 42
```

### Guard conditions

Add an `if` guard to a pattern arm for additional filtering:

```zephyr
let n = 7;
match n {
    x if x % 2 == 0 => print(f"{x} is even"),
    x                => print(f"{x} is odd"),
}
// 7 is odd
```

### Result\<T\> matching

```zephyr
match divide(10, 0) {
    Ok(v)    => print(f"result: {v}"),
    Err(msg) => print(f"error: {msg}"),
}
```

### Exhaustiveness

The compiler checks that all possible variants of an enum are covered. Missing a case is a compile-time error unless `_` is present as a catch-all.
