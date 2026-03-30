## Enum

Enums are sum types — a value is exactly one of a fixed set of variants.

```zephyr
enum Direction {
    North,
    South,
    East,
    West,
}

let dir = Direction::North;
```

### Variants with payloads

Enum variants can carry data:

```zephyr
enum Shape {
    Circle(float),            // radius
    Rect(float, float),       // width, height
    Point,
}

let s = Shape::Circle(3.0);
let r = Shape::Rect(4.0, 5.0);
```

### Pattern matching on enums

```zephyr
fn area(s: Shape) -> float {
    match s {
        Shape::Circle(r)       => return 3.14159 * r * r,
        Shape::Rect(w, h)      => return w * h,
        Shape::Point           => return 0.0,
    }
}

print(area(Shape::Circle(1.0)));     // 3.14159
print(area(Shape::Rect(3.0, 4.0))); // 12
```

### Result\<T\> as a built-in enum

`Result<T>` is a special built-in enum with two variants:

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}
```

```zephyr
fn safe_div(a: int, b: int) -> Result<int> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}
```

See [Types](types.md) for `?` propagation.
