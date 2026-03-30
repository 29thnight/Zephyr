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

Compiled modules are cached as `.zphc` files. The compiler automatically invalidates the cache if the source file is modified.
