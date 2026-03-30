# Control Flow

Zephyr relies on familiar foundational syntax to alter an execution logic routine.

## If / Else Iterations

The canonical standard block syntax `if` branches boolean expressions. Like Rust, wrapping conditionals with enclosing parentheses `()` is considered completely optional.

```zephyr
let mut score = 85;

if score >= 90 {
    print("Grade: A");
} else if score >= 80 {
    print("Grade: B");
} else {
    print("Keep playing!");
}
```

## Loops & Iterations

### While

The native `while` conditional blocks operate continuously up until an expression returns `false` or encounters a halting token.

```zephyr
mut count = 0;
while count < 3 {
    print(count);
    count += 1;
}
```

### For In

Use the `for ... in` declaration mapping to inherently loop array, map bounds, or standard range generators. Zephyr’s `in` iterator expands objects transparently reducing overhead index queries.

```zephyr
let enemies = ["Goblin", "Slime", "Orc"];

for e in enemies {
    print(e);
}
```

If manipulating absolute numerical loops manually, rely on the implicit generator syntax `start..end`. Note that the range generates up to `end` implicitly excluding the tailing limit (not inclusive).

```zephyr
// Computes 0, 1, 2, 3, 4
for i in 0..5 {
    print(i);
}
```

## Branch Halting Modifiers

Inside iterative logic segments, keyword interrupts dictate loop flow gracefully:

- **`break`**: Destroys the nearest contiguous repeating layer structure natively and skips right ahead towards executing subsequent code.
- **`continue`**: Immediately restarts the next sequential iteration loop context evaluation cycle, skipping the current trailing instructions.

```zephyr
mut limit = 0;
while true {
    if limit == 5 { break; }     // Jumps outside
    
    limit += 1;
    if limit == 2 { continue; }  // Discards 'limit == 2' stdout
    
    print(limit);
}
```
