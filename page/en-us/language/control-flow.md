# Control Flow

Zephyr leverages robust pattern extraction into standard loop mechanics, reducing repetitive unrolling overheads during iterative parsing.

## Branches (`if`, `if let`)
```zephyr
if value > 0 {
  print("positive");
} else {
  print("non-negative");
}

// Pattern extraction destructuring evaluated directly as branching bounds
if let Event::Hit(Hit { damage, crit: true }) = event {
  print(damage);
}
```

## Loops (`while`, `while let`, `for-in`)
```zephyr
// Continuous pair extractor using implicit iterators
while let [name, hp] = next_pair() {
  print(name);
}

// Sequence iteration
for item in values {
  print(item);
}

// Range bounds iteration (exclusive)
for i in 0..n {
  print(i);
}

// Range bounds iteration (inclusive)
for i in 0..=n {
  print(i);
}
```
