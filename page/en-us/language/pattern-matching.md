# Pattern Matching (`match`)

A transparent and incredibly safe system capable of destructuring inner fields, dissecting arrays, and comparing explicit boundary payloads without triggering null pointer crashes.

## Examples
By compounding wildcards (`_`), `OR` bounds, struct destructuring, tuple slicing, and conditional `if guards`, runtime branching complexity diminishes substantially.

```zephyr
// Enum & Struct Extraction + Guard
match event {
  Event::Hit(Hit { damage, crit: true }) if damage > 5 => damage,
  Event::None | Event::Hit(_) => 0,
}

// Range evaluation mapping
match value {
  0..10 => "low",
  10..=20 => "mid",
  _ => "high",
}

// Tuple bindings
match point {
  (x, y) => x + y,
}

// Array boundary matching
match pair {
  [lhs, rhs] => lhs + rhs,
  _ => 0,
}
```
