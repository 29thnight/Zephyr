## Trait

Traits define a shared interface — a named set of method signatures that types must implement.

```zephyr
trait Animal {
    fn sound(self) -> string;
    fn name(self) -> string;
}
```

### Implementing a trait

```zephyr
struct Dog { breed: string }
struct Cat { indoor: bool }

impl Animal for Dog {
    fn sound(self) -> string { return "Woof"; }
    fn name(self)  -> string { return "Dog"; }
}

impl Animal for Cat {
    fn sound(self) -> string { return "Meow"; }
    fn name(self)  -> string { return "Cat"; }
}
```

The compiler (semacheck pass 2) verifies that all trait methods are present in the `impl` block. A missing method is a compile-time error.

### Calling trait methods

```zephyr
let d = Dog { breed: "Labrador" };
print(d.sound());   // Woof
print(d.name());    // Dog
```

### Multiple traits

A type can implement any number of traits:

```zephyr
trait Drawable {
    fn draw(self) -> void;
}

trait Resizable {
    fn resize(self, factor: float) -> void;
}

struct Sprite { width: float, height: float }

impl Drawable for Sprite {
    fn draw(self) -> void {
        print(f"Sprite({self.width}x{self.height})");
    }
}

impl Resizable for Sprite {
    fn resize(self, factor: float) -> void {
        // ... mutation not shown here
    }
}
```

### Trait bounds (where clauses)

Use where clauses to constrain generic type parameters to types that implement a trait:

```zephyr
trait Comparable {
    fn less_than(self, other: Self) -> bool;
}

fn min<T>(a: T, b: T) -> T where T: Comparable {
    if a.less_than(b) { return a; }
    return b;
}
```

### impl without a trait

An `impl` block without a `for Trait` clause adds methods directly to a struct:

```zephyr
struct Vec2 { x: float, y: float }

impl Vec2 {
    fn length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
    fn zero() -> Vec2 {
        return Vec2 { x: 0.0, y: 0.0 };
    }
}
```
