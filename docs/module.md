## Module

Zephyr organises code into modules — individual `.zph` files that export bindings for other files to import.

### Exporting

Mark any top-level declaration with `export`:

```zephyr
// math_utils.zph
export fn square(x: float) -> float { return x * x; }
export fn cube(x: float) -> float { return x * x * x; }
export let PI = 3.14159;
```

### Importing

**Default import** — brings all exports into scope:

```zephyr
import "math_utils";
print(square(3.0));   // 9
print(PI);            // 3.14159
```

**Named import** — cherry-pick specific exports:

```zephyr
import { square, PI } from "math_utils";
print(square(4.0));
```

**Namespace alias** — prefix all exports with an alias:

```zephyr
import "math_utils" as math;
print(math.square(5.0));
print(math.PI);
```

### Re-exports

A module can re-export another module's bindings:

```zephyr
// index.zph
export { square, cube } from "math_utils";
export { Vec2, Vec3 } from "vectors";
```

### Host modules

A C++ host can register modules that appear as first-class imports:

```cpp
vm.register_module("engine", [](ZephyrRuntime& rt) {
    rt.set_function("spawn", spawn_entity);
    rt.set_function("destroy", destroy_entity);
});
```

```zephyr
import "engine";
let e = spawn("player");
```

### package.toml

A package manifest describes a multi-file project:

```toml
[package]
name = "my_game"
version = "0.1.0"
entry  = "src/main.zph"

[dependencies]
utils = "src/utils"
```

Call `set_package_root()` from the host to activate package resolution:

```cpp
vm.set_package_root("my_game/");
```

Imports then resolve relative to the package root:

```zephyr
import "utils";    // resolves to my_game/src/utils.zph
```

### Module bytecode caching

Compiled modules are cached as `.zphc` files next to the source. The cache stores the source mtime and is automatically invalidated on change, so only modified modules are recompiled.
