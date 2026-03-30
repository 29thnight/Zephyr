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
