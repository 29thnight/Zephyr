## Array

Arrays are ordered, dynamically-sized collections. Elements are of type `any` unless a typed wrapper is used.

### Literals

```zephyr
let nums = [1, 2, 3, 4, 5];
let names = ["Alice", "Bob", "Carol"];
let mixed: any = [1, "two", true];
```

### Access and mutation

```zephyr
let a = [10, 20, 30];
print(a[0]);    // 10
print(a[2]);    // 30

mut b = [1, 2, 3];
b[1] = 99;
print(b);       // [1, 99, 3]
```

### Length

```zephyr
print(len([1, 2, 3]));   // 3
```

### push / concat

```zephyr
mut arr = [1, 2, 3];
push(arr, 4);
print(arr);   // [1, 2, 3, 4]

let merged = concat([1, 2], [3, 4]);
print(merged);   // [1, 2, 3, 4]
```

### join

```zephyr
let words = ["Hello", "World"];
print(join(words, " "));   // Hello World
```

### Iterating

```zephyr
let items = ["a", "b", "c"];
for item in items {
    print(item);
}
```

With index:

```zephyr
for i in range(0, len(items)) {
    print(f"{i}: {items[i]}");
}
```

### std/collections

For typed maps, sets, and queues, use `std/collections`:

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";

let m = Map::new();
m.set("key", 42);
print(m.get("key"));   // 42
print(m.has("key"));   // true

let s = Set::new();
s.add(1);
s.add(2);
s.add(1);
print(s.size());       // 2

let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());    // first
```
