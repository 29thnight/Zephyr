## Struct

Structs are named product types with typed fields.

```zephyr
struct Point {
    x: float,
    y: float,
}
```

### Construction

```zephyr
let p = Point { x: 3.0, y: 4.0 };
```

All fields must be provided; there are no default field values.

### Field access

```zephyr
print(p.x);   // 3
print(p.y);   // 4
```

### Mutation

Fields are mutable on a `mut` binding:

```zephyr
mut q = Point { x: 0.0, y: 0.0 };
q.x = 10.0;
q.y = 5.0;
```

### Methods via impl

Add behaviour to a struct with an `impl` block:

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

let r = Rect { width: 4.0, height: 3.0 };
print(r.area());             // 12
let big = r.scale(2.0);
print(big.area());           // 48
```

### Trait implementation

```zephyr
trait Printable {
    fn display(self) -> void;
}

impl Printable for Rect {
    fn display(self) -> void {
        print(f"Rect({self.width} x {self.height})");
    }
}

r.display();   // Rect(4 x 3)
```

See [Trait](trait.md) for full trait details.

### Nested structs

```zephyr
struct Circle {
    center: Point,
    radius: float,
}

let c = Circle {
    center: Point { x: 0.0, y: 0.0 },
    radius: 5.0,
};

print(c.center.x);   // 0
```
