# Generics

Generics provide a mechanism for parameterizing functions and structures over types, enabling code reuse with full static type safety.

## Generic Functions

Functions can define one or more type parameters within angle brackets `<>`.

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

let a = identity(10);      // T is int
let b = identity("hi");    // T is string
```

## Trait Bounds (where clauses)

Type parameters can be constrained to only accept types that implement specific traits using the `where` clause.

```zephyr
trait Printable {
    fn display(self) -> void;
}

fn log<T>(item: T) -> void where T: Printable {
    item.display();
}
```

### Multiple Constraints

Multiple trait bounds can be combined using the `+` operator.

```zephyr
fn process<T>(item: T) -> void where T: Printable + Comparable {
    // ...
}
```

Multiple constraints with `+`:

```zephyr
fn process<T>(x: T) -> void where T: Printable + Comparable {
    x.display();
}
```

## Generic structs

```zephyr
struct Pair<A, B> {
    first: A,
    second: B,
}

let p = Pair<int, string> { first: 1, second: "one" };
print(p.first);    // 1
print(p.second);   // one
```

## Generic impl blocks

```zephyr
impl<A, B> Pair<A, B> {
    fn swap(self) -> Pair<B, A> {
        return Pair { first: self.second, second: self.first };
    }
}
```

## `Result<T>` in practice

`Result<T>` is a generic enum built into the language:

```zephyr
fn read_config(path: string) -> Result<string> {
    let content = read_file(path)?;
    return Ok(content);
}

fn main() -> void {
    match read_config("config.toml") {
        Ok(text) => print(text),
        Err(e)   => print(f"Failed: {e}"),
    }
}
```

## Monomorphisation

Generics are resolved at compile time. Each unique instantiation (e.g., `identity<int>` and `identity<string>`) generates a separate bytecode function. There is no runtime type erasure.
