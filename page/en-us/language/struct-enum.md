# Struct & Enum

We strictly support Algebraic Data Types (Enums) and Structural records (Structs) to guarantee secure layout behaviors across the C++ bridge.

## Struct 
Organizes and groups native attributes. It also supports field-shorthand initialization.

```zephyr
struct Player {
  hp: int,
  name: string,
}

let x = 3;
let y = 4;
let p1 = Player { hp: 10, name: "neo" };
let p2 = Vec2 { x, y }; // field shorthand integration
```

## Enum
Enums act as robust branches utilizing runtime payloads, not just plain constants. They serve as the predominant layout for reliable state mechanics.

```zephyr
enum State {
  Idle,
  Hurt(int), // int payload containing taken damage amount
}
```
