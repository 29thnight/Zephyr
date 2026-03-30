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

> If an imported member is completely unresolved or lacks the `export` keyword, Zephyr aggressively aborts its compilation node right away avoiding dangerous runtime evaluation.
