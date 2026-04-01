# Traits & Impl

Provides structural abstraction capabilities resembling interfaces or C++ pure abstract behavior.

## Defining a Trait

Traits define a shared interface — a named set of method signatures that types must implement.

```zephyr
trait Animal {
    fn sound(self) -> string;
    fn name(self) -> string;
}
```

## Implementing a Trait

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

The compiler verifies that all trait methods are present in the `impl` block. A missing method is a compile-time error.

### Calling Trait Methods

```zephyr
let d = Dog { breed: "Labrador" };
print(d.sound());   // Woof
print(d.name());    // Dog
```

---

## Multiple Traits

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
        // mutation logic
    }
}
```

---

## Trait Bounds (where clauses)

Use `where` clauses to constrain generic type parameters to types that implement specific traits:

```zephyr
trait Comparable {
    fn less_than(self, other: Self) -> bool;
}

fn min<T>(a: T, b: T) -> T where T: Comparable {
    if a.less_than(b) { return a; }
    return b;
}
```

---

## impl without a Trait

An `impl` block without a `for Trait` clause adds methods directly to a struct (often called "associated methods"):

```zephyr
struct Vec2 { x: float, y: float }

impl Vec2 {
    fn length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}
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
