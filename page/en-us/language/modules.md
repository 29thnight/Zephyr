# Modules & Imports

Splits prolonged logics into independent component packages.

## Loading external symbols
Fetching variables, interfaces, or classes residing outside the context.

```zephyr
// Imports entirety of the external namespace explicitly
import "foo.zph";

// Narrows down collision margins masking with an Alias
import "foo.zph" as foo;

// C++ Global builtin namespaces
import "engine";

// Opens the function accessibility towards other import nodes
export fn run() -> int {
  return 1;
}
```

> [!NOTE] Dependency Static Defenses
> If an imported member is completely unresolved or lacks the `export` keyword, Zephyr aggressively aborts its compilation node right away avoiding dangerous runtime evaluation.
