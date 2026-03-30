# Structs & Enums

The bulk of state logic and data grouping in Zephyr leverages composite data structures closely mirroring Rust's syntax.

## Struct Definitions

A `struct` holds a cluster of grouped metadata fields under a single structural type tree.

```zephyr
struct Player {
    name: string,
    hp: int,
    is_admin: bool,
}

// Construction layout
let mut p = Player {
    name: "Zephyr",
    hp: 100,
    is_admin: true,
};

// Access mutation
p.hp -= 10;
print(p.name);
```

## Enum Expressions

Enums manifest distinct variant payloads. Unlike typical C integers, Zephyr enums are Algebraic Data Types (ADT) holding customized underlying tuples of multiple embedded structs.

```zephyr
enum Event {
    Quit,
    Move(x: float, y: float),
    Say(message: string),
}

let evt = Event::Move(10.5, 20.0);
```

## Implementation Blocks (`impl`)

Unlike heavy prototype chaining, methods and static associations are appended strictly outwards referencing the struct's specific `impl` scope.

The first parameter of a bound method must always be `self`.

```zephyr
struct Vec2 {
    x: float,
    y: float,
}

impl Vec2 {
    // Static association (no 'self')
    fn new(x: float, y: float) -> Vec2 {
        return Vec2 { x: x, y: y };
    }

    // Method association 
    fn magnitude(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}

let v = Vec2::new(3.0, 4.0);
print(v.magnitude()); // 5.0
```
