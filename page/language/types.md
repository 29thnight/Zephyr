# Type Model

Zephyr integrates a dynamically robust type system wrapped underneath strict compile-time type verification passes.

## Primitive Types

There are four essential primitives stored directly by the register layout:
- `int` (Represented by 64-bit signed integers)
- `float` (Represented by 64-bit floating point precision IEEE 754)
- `bool` (`true` or `false`)
- `string` (Immutable UTF-8 byte arrays)

```zephyr
let hp: int = 100;
let speed: float = 1.5;
let is_alive: bool = true;
let name: string = "Hero";
```

## String Interpolation

Zephyr offers embedded string formatting (Template Literals) denoted by an `f` prefix.

```zephyr
let entity = " Goblin";
print(f"I found a{entity} with {hp} hitpoints.");
```

## Collection Traits

### Arrays

Arrays are inherently mutable resizable lists housing homogeneous values. To iterate them sequentially, use explicit `[index]` bracket notation or `for ... in` loop structures.

```zephyr
mut my_array = [10, 20, 30];
my_array[0] = 50;

print(my_array[0]); // 50
print(my_array.length()); // 3
```

### Result Type (`Result<T>`)

Since Zephyr removes traditional implicit `try / catch` flows commonly known for poor game scaling, all potentially fatal fallible logic throws an explicit built-in standard `Result<T>` Enum type.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}
```

This enforces explicit manual verification and encourages matching to securely parse state returns.

```zephyr
fn get_file(path: string) -> Result<string> {
    if exists(path) {
        return Ok("content");
    }
    return Err("File not found");
}
```
