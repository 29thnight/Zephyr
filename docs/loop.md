## Loops

### while

```zephyr
mut i = 0;
while i < 5 {
    print(i);
    i += 1;
}
// 0 1 2 3 4
```

### for in

Iterate over a range or array:

```zephyr
for i in range(0, 5) {
    print(i);
}
// 0 1 2 3 4
```

Iterating an array:

```zephyr
let names = ["Alice", "Bob", "Carol"];
for name in names {
    print(name);
}
```

### break

Exit the innermost loop immediately:

```zephyr
mut i = 0;
while true {
    if i >= 3 { break; }
    print(i);
    i += 1;
}
// 0 1 2
```

### continue

Skip to the next iteration:

```zephyr
for i in range(0, 6) {
    if i % 2 == 0 { continue; }
    print(i);
}
// 1 3 5
```

### Iterator protocol

Types that implement the built-in iterator protocol can be used directly in `for in`:

```zephyr
trait Iterator {
    fn has_next(self) -> bool;
    fn next(self) -> any;
}
```

The `std/collections` types (`Map`, `Set`, `Queue`) all implement this protocol.

```zephyr
import { Map } from "std/collections";

let scores = Map::new();
scores.set("Alice", 100);
scores.set("Bob", 95);

for entry in scores {
    print(f"{entry.key}: {entry.value}");
}
```
