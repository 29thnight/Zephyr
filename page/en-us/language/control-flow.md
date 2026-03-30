# Control Flow

Zephyr provides standard branching and looping constructs for managing execution flow. 

## Branches (if / else)

Conditional branching is performed via the `if` and `else` keywords. Braces `{}` are mandatory for all blocks.

```zephyr
if value > 10 {
    print("Greater than 10");
} else if value == 10 {
    print("Exactly 10");
} else {
    print("Less than 10");
}
```

### if let

The `if let` construct combines pattern matching with conditional branching. The block executes only if the pattern successfully matches the input value.

```zephyr
if let Ok(data) = get_resource() {
    print(data);
}
```

## Loops

Zephyr supports several looping mechanisms for repetitive execution.

### while

The `while` loop continues execution as long as the specified boolean condition remains `true`.

```zephyr
let mut i = 0;
while i < 5 {
    print(i);
    i += 1;
}
```

### while let

Similar to `if let`, the `while let` loop continues as long as the pattern successfully matches the value returned by the expression.

```zephyr
while let Some(msg) = queue.pop() {
    process(msg);
}
```

### for-in

The `for-in` loop is used to iterate over a sequence, such as a range or a collection that implements the iterator protocol.

```zephyr
// Exclusive range
for i in 0..5 {
    print(i); // 0, 1, 2, 3, 4
}

// Inclusive range
for i in 0..=5 {
    print(i); // 0, 1, 2, 3, 4, 5
}
```

// Sequence iteration
for item in values {
    print(item);
}

// Range bounds iteration
for i in 0..n {
    print(i);
}

// Range bounds iteration (inclusive)
for i in 0..=n {
    print(i);
}
```

## Break & Continue

You can manipulate loop execution flow using `break` to exit the loop or `continue` to skip the rest of the current iteration:

```zephyr
mut i = 0;
while true {
    if i >= 3 { break; } // Exit loop entirely
    print(i);
    i += 1;
}
// 0 1 2

for i in 0..6 {
    if i % 2 == 0 { continue; } // Skip to next iteration
    print(i);
}
// 1 3 5
```

## Iterator Protocol

Types that implement the built-in iterator protocol can be used directly in `for in` loops:

```zephyr
trait Iterator {
    fn has_next(self) -> bool;
    fn next(self) -> any;
}
```

The `std/collections` types (`Map`, `Set`, `Queue`) all implement this protocol out of the box.

```zephyr
import { Map } from "std/collections";

let scores = Map::new();
scores.set("Alice", 100);
scores.set("Bob", 95);

for entry in scores {
    print(f"{entry.key}: {entry.value}");
}
```

---

## Returning from Functions

The `return` keyword is used to exit the current function and optionally return a value.

```zephyr
fn sign(x: int) -> int {
    if x > 0 { return  1; }
    if x < 0 { return -1; }
    return 0;
}
```

---

## Early Return and Chaining

Zephyr provides concise syntax for managing early exit conditions.

### Error Propagation (`?`)

The `?` operator on a `Result<T>` expression returns `Err` early from the enclosing function if the result is an error.

```zephyr
fn load() -> Result<string> {
    let data = read_file("input.txt")?;   // returns early on Err
    return Ok(data);
}
```

### Optional Chaining (`?.`)

The `?.` operator short-circuits the entire expression to `nil` if any receiver in the chain is `nil`.

```zephyr
let tag = doc?.header?.title;
```
