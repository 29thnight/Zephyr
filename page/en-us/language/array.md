# Arrays

Arrays in Zephyr are ordered, dynamically-sized collections of elements. By default, elements are of type `any` unless specified otherwise.

## Array Literals

Arrays are initialized using square brackets `[]`.

```zephyr
let numbers = [1, 2, 3];
let strings = ["a", "b", "c"];
let empty = [];
```

## Access and Mutation

Elements are accessed via zero-based indexing. Modifying an array requires a `mut` binding.

```zephyr
let a = [10, 20, 30];
let first = a[0]; // 10

mut b = [1, 2, 3];
b[0] = 100;
```

## Length

The current number of elements in an array is retrieved using the `len()` function.

```zephyr
let count = len([1, 2, 3]); // 3
```

## push / concat

```zephyr
mut arr = [1, 2, 3];
push(arr, 4);
print(arr);   // [1, 2, 3, 4]

let merged = concat([1, 2], [3, 4]);
print(merged);   // [1, 2, 3, 4]
```

## join

```zephyr
let words = ["Hello", "World"];
print(join(words, " "));   // Hello World
```

## Iterating

```zephyr
let items = ["a", "b", "c"];
for item in items {
    print(item);
}
```

With an index:

```zephyr
for i in range(0, len(items)) {
    print(f"{i}: {items[i]}");
}
```

## `std/collections`

For strongly-typed maps, sets, queues, and stacks, use the `std/collections` module:

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
