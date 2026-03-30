# Modules & Packages

Zephyr promotes highly cohesive boundaries by splitting independent source code cleanly across `.zph` extension limits referred natively as Packages/Modules, leveraging explicit bounded `import`/`export` semantics mapping.

## Transmitting Bindings (`import` / `export`)

To expose identifiers dynamically outwards towards foreign external scopes, prepend definitions referencing the standalone `export` keyword.

```zephyr
// math_utils.zph
export fn square(x: float) -> float { return x * x; }
export let PI = 3.14159;
```

Outside callers (Importing Environments) fetch payload structures parsing multiple patterns:

1. **Default Aggregate Import**: Imports absolutely everything collapsing exported functions linearly towards raw global mappings.
```zephyr
import "math_utils";
print(PI);
```

2. **Named Disaggregated Import**: Destructures specified metadata references directly efficiently ignoring unrelated functions tightly.
```zephyr
import { square } from "math_utils";
```

3. **Namespace Alias Referencing**: Binds targeted dependencies alongside scoped structural names (Aliases) inherently bypassing raw naming collision hazards.
```zephyr
import "math_utils" as math;
print(math.PI);
```

> Module bindings also endorse re-exports (`export { func } from "other";`) wrapping encapsulated external packages transparently to upper-layer dependencies cleanly! 

## Injecting Built-In C++ Host Modules

Internal Engine pipelines (C++) dynamically dispatch modules overriding standard `.zph` scripts utilizing VM injection bindings securely mirroring conventional script references!

```cpp
vm.register_module("engine", [](ZephyrRuntime& rt) {
    rt.set_function("spawn", spawn_entity);
});
```

```zephyr
import "engine";
let e = spawn("player");
```

## Automatic Bytecode Caching (`.zphc`)

During compilation phase translation boundaries, Zephyr compiles AST outputs directly converting `.zph` bounds toward specialized optimized caching clusters (`.zphc`).
Consequently, any re-initialization procedures skipping modification timestamps actively bypass extensive lexer passes accelerating structural boot sequences blazingly!

## Package Configurations (`package.toml`)

Managing scalable root pipelines leverages native dependency manifestations (`package.toml`) linking standard source packages together hierarchically.

```toml
[package]
name = "my_game"
version = "0.1.0"
entry = "src/main.zph"

[dependencies]
math = "std/math"
utils = "src/utils"
```

C++ Hosts sequentially point references outwards utilizing `ZephyrVM::set_package_root()` anchoring default startup procedures tightly spanning multi-project structures seamlessly.
