# Pattern Matching

The `match` expression provides a secure and powerful way to destructively extract Enum properties through robust path branching and exhaustiveness checks.

## Exhaustiveness Verification

Whenever an enum branch evaluates a `match` case, all enclosed underlying variants **must** be mapped appropriately. If trailing paths are neglected, it issues a fatal AST semantic compilation error.

```zephyr
enum Status { Alive, Dead, Unknown }

let s = Status::Alive;
match s {
    Alive => print("Still kicking"),
    Dead  => print("Game Over"),
    // If Unknown is missing here, the compiler will trigger an error.
    Unknown => print("???"),
}
```

To easily bypass exhaustive maps efficiently across vast enumerations, install a fallback catch-all variable or placeholder (`_`).

```zephyr
match s {
    Alive => print("Still kicking"),
    _     => print("Passed out"), // Handles (Dead, Unknown)
}
```

## Extracting Tuple Variants

The true power of `match` manifests when unrolling the contents tightly baked within an Algebraic Data Enum type.

```zephyr
enum Message {
    Quit,
    Chat(player: string, content: string),
}

let event = Message::Chat("Hero", "Hello!");

match event {
    Quit => exit(),
    Chat(p, c) => print(f"[{p}] said: {c}"), 
}
```

In scenarios that require partial destructing validation (e.g. tracking when a variable surpasses a threshold limit), utilize Pattern **Guards** via conditional checking appended at the suffix of an extraction map.

```zephyr
match event {
    Chat(p, c) if p == "Admin" => {
        print("Admin invoked command.");
    },
    Chat(p, c) => print(c),
    _ => (),
}
```
