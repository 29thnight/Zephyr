# Declarations

How to declare the building blocks (Variables, Functions) of Zephyr.

## Variables (`let` / `mut`)

By default, variables are allocated immutably. You may optionally append type hints.
```zephyr
let score = 10;
let hp: int = 100;
```

To modify its value later alongside the VM iteration, append the `mut` keyword.
```zephyr
let mut offset = 0;
offset += 5;
```

## Functions (`fn`)

A fundamental logic encapsulation signature.
```zephyr
fn calculate(attack: int, defense: int) -> int {
  return attack - defense;
}
```

For specialized types such as Enums, Structs, Closures, or Coroutines, please review deep dive contents mapped on the left sidebar.
