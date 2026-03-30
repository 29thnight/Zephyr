## Standard Library

The Zephyr standard library is written in Zephyr itself. Import individual modules as needed.

### std/math

```zephyr
import "std/math";

print(sqrt(16.0));          // 4
print(abs(-7.5));           // 7.5
print(floor(3.9));          // 3
print(ceil(3.1));           // 4
print(clamp(15.0, 0.0, 10.0)); // 10
print(lerp(0.0, 10.0, 0.5));   // 5
print(sin(PI / 2.0));          // 1
print(cos(0.0));               // 1
```

| Function | Signature |
|---|---|
| `sqrt(x)` | `float -> float` |
| `abs(x)` | `float -> float` |
| `floor(x)` | `float -> float` |
| `ceil(x)` | `float -> float` |
| `clamp(x, min, max)` | `float, float, float -> float` |
| `lerp(a, b, t)` | `float, float, float -> float` |
| `sin(x)` | `float -> float` |
| `cos(x)` | `float -> float` |
| `tan(x)` | `float -> float` |
| `pow(base, exp)` | `float, float -> float` |
| `log(x)` | `float -> float` |
| `PI` | constant `float` |

### std/string

```zephyr
import "std/string";

print(trim("  hello  "));          // hello
print(to_upper("hello"));          // HELLO
print(to_lower("WORLD"));          // world
print(replace("foo bar", "bar", "baz")); // foo baz
print(split("a,b,c", ","));        // ["a", "b", "c"]
print(starts_with("hello", "he")); // true
print(ends_with("hello", "lo"));   // true
```

### std/collections

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";

// Map<K, V>
let m = Map::new();
m.set("a", 1);
print(m.get("a"));    // 1
print(m.has("a"));    // true
m.delete("a");
print(m.size());      // 0

// Set<T>
let s = Set::new();
s.add(10);
s.add(20);
s.add(10);            // duplicate ignored
print(s.size());      // 2
print(s.has(10));     // true

// Queue<T>
let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());   // first
print(q.size());      // 1

// Stack<T>
let stk = Stack::new();
stk.push(1);
stk.push(2);
print(stk.pop());     // 2
```

### std/json

```zephyr
import "std/json";

let obj: any = parse("{\"x\": 1, \"y\": 2}");
match obj {
    Ok(v)    => print(stringify(v)),
    Err(msg) => print(f"parse error: {msg}"),
}
```

| Function | Signature |
|---|---|
| `parse(s)` | `string -> Result<any>` |
| `stringify(v)` | `any -> string` |

### std/io

```zephyr
import "std/io";

let content = read_file("data.txt");
match content {
    Ok(text) => print(text),
    Err(e)   => print(f"error: {e}"),
}

write_file("out.txt", "Hello!\n");

let lines = read_lines("data.txt");
match lines {
    Ok(ls) => for line in ls { print(line); },
    Err(e) => print(e),
}
```

### std/gc

Direct GC control from script:

```zephyr
import "std/gc";

gc.collect_young();    // minor collection
gc.collect();          // full collection
gc.step(500);          // incremental step (500 µs budget)
let s = gc.stats();    // returns map with pause/allocation stats
```

### std/profiler

```zephyr
import "std/profiler";

profiler.start();
// ... code to profile ...
profiler.stop();
profiler.report();   // prints sampling data to stdout
```

Or use the `--profile` CLI flag to enable profiling for an entire run:

```bash
zephyr run --profile mygame.zph
# writes zephyr_profile.json
```
