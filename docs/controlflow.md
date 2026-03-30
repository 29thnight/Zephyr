## Control Flow

### if / else

```zephyr
let x = 10;

if x > 5 {
    print("big");
} else if x == 5 {
    print("five");
} else {
    print("small");
}
```

Conditions do not require parentheses around the expression. The body must always be a block (`{ }`).

### match

`match` dispatches on a value using pattern matching. All cases must be exhaustive or include a wildcard `_`:

```zephyr
let n = 2;

match n {
    1 => print("one"),
    2 => print("two"),
    3 => print("three"),
    _ => print("other"),
}
```

See [Pattern Matching](match.md) for the full pattern syntax.

### return

`return` exits the current function with a value:

```zephyr
fn sign(x: int) -> int {
    if x > 0 { return  1; }
    if x < 0 { return -1; }
    return 0;
}
```

### Error propagation with `?`

`?` on a `Result<T>` expression returns `Err` early from the enclosing function if the result is an error:

```zephyr
fn load() -> Result<string> {
    let data = read_file("input.txt")?;   // returns early on Err
    return Ok(data);
}
```

### Optional chaining with `?.`

`?.` short-circuits to `nil` when the receiver is `nil`:

```zephyr
let tag = doc?.header?.title;
```
