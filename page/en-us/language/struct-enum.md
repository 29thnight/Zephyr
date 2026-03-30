# Structs and Enums

Zephyr uses Structs for product types (records) and Enums for sum types (variants). These are the primary data structures for organizing data and state.

## Structs

A `struct` is a named collection of typed fields.

```zephyr
struct Vec2 {
    x: float,
    y: float,
}
```

### Construction

Instances are created using struct literals. All fields must be initialized.

```zephyr
let pos = Vec2 { x: 10.0, y: 20.0 };

// Field shorthand initialization
let x = 5.0;
let y = 5.0;
let origin = Vec2 { x, y };
```

### Field Access and Mutation

Fields are accessed via dot notation. Mutation requires a `mut` binding.

```zephyr
mut player_pos = Vec2 { x: 0.0, y: 0.0 };
player_pos.x = 100.0;
print(player_pos.x); // 100
```

### Methods (impl)

Behavior can be added to structs using `impl` blocks. Methods receive `self` as the first parameter.

```zephyr
struct Rect {
    width: float,
    height: float,
}

impl Rect {
    fn area(self) -> float {
        return self.width * self.height;
    }

    fn scale(self, factor: float) -> Rect {
        return Rect {
            width: self.width * factor,
            height: self.height * factor,
        };
    }
}

let r = Rect { width: 10.0, height: 5.0 };
print(r.area()); // 50.0
```

### Nested Structs

Structs can contain other struct instances as fields.

```zephyr
struct Circle {
    center: Vec2,
    radius: float,
}

let c = Circle {
    center: Vec2 { x: 0.0, y: 0.0 },
    radius: 10.0,
};
```

---

## Trait Implementation

Structs can implement traits to provide a shared interface.

```zephyr
trait Printable {
    fn display(self) -> void;
}

impl Printable for Rect {
    fn display(self) -> void {
        print(f"Rect({self.width} x {self.height})");
    }
}

r.display();   // Rect(10 x 5)
```

See the [Traits](./traits.md) page for full details.

---

## Enums

An `enum` defines a type that can be one of several variants.

```zephyr
enum Color {
    Red,
    Green,
    Blue,
}

let c = Color::Red;
```

### Variants with Payloads

Variants can carry associated data (payloads).

```zephyr
enum Shape {
    Circle(float),          // radius
    Rect(float, float),     // width, height
    Point,                  // no payload
}

let s1 = Shape::Circle(5.0);
let s2 = Shape::Rect(10.0, 20.0);
```

### Pattern Matching

Enums are typically deconstructed using `match` expressions.

```zephyr
fn get_area(s: Shape) -> float {
    match s {
        Shape::Circle(r)  => return 3.14 * r * r,
        Shape::Rect(w, h) => return w * h,
        Shape::Point      => return 0.0,
    }
}
```

### Result\<T\>

`Result<T>` is a built-in enum used for error handling.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}

fn divide(a: int, b: int) -> Result<int> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}
```
