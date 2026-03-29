# Functions & Closures

The primary unit for packaging executable blocks. Optimized naturally to attach or dispatch logic from the C++ Host using `ZephyrClassBinder`.

## Declarations (`fn`)
Explicitly defining input/output bounding signatures.

```zephyr
fn calculate_damage(attack: int, defense: int) -> int {
  return attack - defense;
}
```

## Anonymous Closures
A disposable function literal assigned to variables. Generally handled for delayed callbacks or event listener injections.

```zephyr
let increment = fn(a: int) -> int {
    return a + 1;
};

// ... inject `increment` directly back to Host engine
```
