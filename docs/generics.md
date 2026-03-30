## Generics

Generics allow functions and structs to be parameterised over types.

### Generic functions

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

print(identity(42));        // 42
print(identity("hello"));   // hello
```

### Where clauses

Constrain a type parameter to types implementing a given trait:

```zephyr
trait Printable {
    fn display(self) -> void;
}

fn print_twice<T>(x: T) -> void where T: Printable {
    x.display();
    x.display();
}
```

Multiple constraints with `+`:

```zephyr
fn process<T>(x: T) -> void where T: Printable + Comparable {
    x.display();
}
```

### Generic structs

```zephyr
struct Pair<A, B> {
    first: A,
    second: B,
}

let p = Pair<int, string> { first: 1, second: "one" };
print(p.first);    // 1
print(p.second);   // one
```

### Generic impl blocks

```zephyr
impl<A, B> Pair<A, B> {
    fn swap(self) -> Pair<B, A> {
        return Pair { first: self.second, second: self.first };
    }
}
```

### Result\<T\> in practice

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

### Monomorphisation

Generics are resolved at compile time. Each unique instantiation (e.g., `identity<int>` and `identity<string>`) generates a separate bytecode function. There is no runtime type erasure.
