# Modules and Packages

Zephyr organizes code into modules. Each `.zph` file is an independent module that can export its bindings for use by other modules.

## Exporting

Any top-level declaration (functions, variables, structs, enums, etc.) can be exported using the `export` keyword.

```zephyr
// math.zph
export const PI = 3.14159;
export fn square(x: float) -> float {
    return x * x;
}
```

## Importing

### Default Import
Brings all exported bindings into the current scope.

```zephyr
import "math";
print(square(3.0));   // 9.0
```

### Named Import
Selectively import specific bindings.

```zephyr
import { square, PI } from "math";
```

### Namespace Alias
Prefix all exports with an alias.

```zephyr
import "math" as m;
print(m.square(4.0));
```

---

## Re-exports

A module can re-export another module's bindings:

```zephyr
// collection.zph
export { Map, Set } from "std/collections";
```

---

## Host Modules

C++ hosts can register modules that appear as first-class imports in Zephyr:

```cpp
vm.register_module("engine", [](ZephyrRuntime& rt) {
    rt.set_function("spawn", spawn_entity);
});
```

```zephyr
import "engine";
spawn("player");
```

---

## package.toml

A package manifest describes a multi-file project.

```toml
[package]
name = "my_game"
version = "0.1.0"
entry  = "src/main.zph"

[dependencies]
utils = "src/utils"
```

Use `vm.set_package_root()` in the host to resolve imports relative to the package.

---

## Module Bytecode Caching

Compiled modules are cached as `.zphc` files alongside the source. The compiler compares the source `mtime` on each load and reuses the cache if unchanged, skipping parsing and type-checking entirely.

---

## Standard Library (`std/*`)

Zephyr ships a standard library covering common game scripting needs:

- `std/math`: `sqrt`, `abs`, `lerp`, `clamp`, `sin`, `cos`
- `std/string`: `split`, `trim`, `replace`, `to_upper`
- `std/collections`: `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>`
- `std/json`: `parse(s: string) -> Result<any>`, `stringify(v: any) -> string`
- `std/io`: `read_file`, `write_file`
- `std/gc`, `std/profiler`: GC control and benchmarking tools
