# Traits & Impl

Provides structural abstraction capabilities resembling interfaces or C++ pure abstract behavior.

## Behavior Bounds (`trait`)
Promises a specific method signature contract that types must rigidly fulfill.

```zephyr
trait Drawable {
    fn draw(self) -> void;
    fn get_bounds(self) -> Rect;
}
```

## Implementing a Trait

To implement a trait for a specific type, use the `impl Trait for Type` syntax. The compiler ensures that all methods defined in the trait are present in the implementation block.

```zephyr
struct Sprite {
    id: int,
    pos: Vec2,
}

impl Drawable for Sprite {
    fn draw(self) -> void {
        render_internal(self.id, self.pos);
    }

    fn get_bounds(self) -> Rect {
        return Rect { x: self.pos.x, y: self.pos.y, w: 32.0, h: 32.0 };
    }
}
```

## Trait Bounds (Generics)

Traits can be used as bounds for generic type parameters using the `where` clause. This ensures that the generic type `T` implements the required interface.

```zephyr
fn render_all<T>(items: Array<T>) -> void where T: Drawable {
    for item in items {
        item.draw();
    }
}
```

## Multiple Traits

A single type can implement multiple traits to fulfill different functional requirements.

```zephyr
trait Scalable {
    fn scale(self, factor: float) -> void;
}

impl Scalable for Sprite {
    fn scale(self, factor: float) -> void {
        // Scaling logic
    }
}

// Sprite now implements both Drawable and Scalable.
```

## Self Type

The `Self` keyword can be used within a trait to refer to the type that is implementing the trait.

```zephyr
trait Comparable {
    fn is_equal(self, other: Self) -> bool;
}
```

> [!WARNING] Linting Validations
> Missing even a single implemented method defined on a `trait` interface will immediately cause fatal linkage breakdowns during the parsing `check` phase, averting script crashes at runtime.
