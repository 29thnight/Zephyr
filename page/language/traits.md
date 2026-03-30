# Traits & Generics

Zephyr grants you the ability to compose loosely coupled blueprints scaling robustly beyond object polymorphism implementations. Define common shared behavioral interfaces (`trait`) and implement strongly typed flexible blueprints (`Generics`).

## Declaring Traits

Traits encapsulate abstract signature schemas (interfaces) validating whatever structures promise to inherit them actually cover mapping their associated logic.

```zephyr
trait Animal {
    fn sound(self) -> string;
    fn name(self) -> string;
}
```

### Implementing Traits (`impl`)

To satisfy a trait's boundaries against a customized struct, utilize the `impl Trait for Type` schema path. Notice that Semacheck guarantees runtime predictability by crashing builds deliberately if abstract functions are missing from the `impl` frame.

```zephyr
struct Dog { breed: string }

impl Animal for Dog {
    fn sound(self) -> string { return "Woof"; }
    fn name(self)  -> string { return "Dog"; }
}

let d = Dog { breed: "Labrador" };
print(d.sound()); // Woof
```

Types seamlessly implement diverse multi-faceted traits sequentially.

## Generics `<T>`

Write broadly applicable functional signatures and composite bodies independent of strict mappings using typed `<T>` parameters. Zephyr leverages Compile-Time Monomorphization, translating abstractions into high-optimized specific static opcodes preventing abstract 런타임 캐스팅(Runtime Casting) bloat.

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

print(identity(42));        // 42 (int)
print(identity("hello"));   // hello (string)
```

```zephyr
// Generic Composite Structure Layouts
struct Pair<A, B> {
    first: A,
    second: B,
}
```

### Type Constraints (`where`)

Confine generalized `<T>` boundaries explicitly stating only variants that implement designated traits (`where T: Constraint`) survive Semacheck filters.

```zephyr
trait Comparable {
    fn less_than(self, other: Self) -> bool;
}

fn min<T>(a: T, b: T) -> T where T: Comparable {
    if a.less_than(b) { return a; }
    return b;
}
```

Multiple traits can be coupled conditionally leveraging the `+` operator notation mapping constraints concurrently (e.g. `where T: Printable + Comparable`).
