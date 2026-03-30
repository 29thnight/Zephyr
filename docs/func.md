## Function

Functions are declared with `fn`, take typed parameters, and declare an explicit return type.

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}

print(add(10, 20));   // 30
```

### Void functions

Omit the return value with `-> void`:

```zephyr
fn log(msg: string) -> void {
    print(msg);
}
```

### Default-less parameters

All parameters must be supplied at the call site (unlike Gravity, there are no optional positional args). Use `Result<T>` or `any` for optional patterns.

### Recursion

```zephyr
fn fibonacci(n: int) -> int {
    if n < 2 { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print(fibonacci(10));   // 55
```

### First-class functions

Functions are values and can be stored in variables:

```zephyr
fn double(x: int) -> int { return x * 2; }

let f = double;
print(f(5));   // 10
```

And passed as arguments:

```zephyr
fn apply(f: fn(int) -> int, x: int) -> int {
    return f(x);
}

print(apply(double, 7));   // 14
```

### Anonymous functions

```zephyr
let square = fn(x: int) -> int { return x * x; };
print(square(4));   // 16
```

### Associated functions (static methods)

Functions declared inside an `impl` block and called via `TypeName::fn`:

```zephyr
struct Vec2 { x: float, y: float }

impl Vec2 {
    fn zero() -> Vec2 {
        return Vec2 { x: 0.0, y: 0.0 };
    }
    fn length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}

let origin = Vec2::zero();
let v = Vec2 { x: 3.0, y: 4.0 };
print(v.length());   // 5
```

### Generic functions

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

fn max<T>(a: T, b: T) -> T where T: Comparable {
    if a > b { return a; }
    return b;
}
```

See [Generics](generics.md) for full details.
