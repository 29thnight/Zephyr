# Collections

A suite of advanced data structures (`Map`, `Set`, `Queue`, `Stack`) provided natively to manage game and application states.

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";
```

<div class="custom-features-wrapper">
  <h2>Available Structures</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>Map (Dictionary)</h3>
      <p>Manages unique `Key` and `Value` pairs gracefully. Operates underneath utilizing an efficient O(1) hash table implementation.</p>
    </div>
    <div class="custom-feature-card">
      <h3>Set</h3>
      <p>A collection that implicitly prevents duplicate properties preventing redundant elements effectively mapping existence checks.</p>
    </div>
    <div class="custom-feature-card">
      <h3>Queue & Stack</h3>
      <p><code>Queue</code> is a First-In-First-Out (FIFO) sequential buffer. <code>Stack</code> acts as a Last-In-First-Out (LIFO) recursive model array.</p>
    </div>
  </div>
</div>

## Usage Examples

### Map Usage
```zephyr
let m = Map::new();
m.set("score", 100);

print(m.get("score")); // 100
print(m.has("score")); // true

m.delete("score");     
print(m.size());       // 0
```

### Set Usage
```zephyr
let s = Set::new();
s.add(10);
s.add(20);
s.add(10);             // Ignored (Duplicate)

print(s.size());       // 2
print(s.has(10));      // true 
```

### Queue & Stack Usage
```zephyr
// Queue Usage (FIFO)
let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());    // "first" is returned
print(q.size());       // 1

// Stack Usage (LIFO)
let stk = Stack::new();
stk.push(1);
stk.push(2);
print(stk.pop());      // 2 is returned
```
