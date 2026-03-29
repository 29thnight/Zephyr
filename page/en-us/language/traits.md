# Traits & Impl

Provides structural abstraction capabilities resembling interfaces or C++ pure abstract behavior.

## Behavior Bounds (`trait`)
Promises a specific method signature contract that types must rigidly fulfill.

```zephyr
trait Drawable {
  fn draw(self) -> int;
}
```

## Injection via `impl`
Hooks exact implementations strictly back onto Structs or Enums.

```zephyr
struct Player { x: int, y: int }

impl Drawable for Player {
  fn draw(self) -> int {
    return self.x + self.y;
  }
}
```

> [!WARNING] Linting Validations
> Missing even a single implemented method defined on a `trait` interface will immediately cause fatal linkage breakdowns during the parsing `check` phase, averting script crashes at runtime.
