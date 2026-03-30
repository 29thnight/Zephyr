## Syntax

### Comments

```zephyr
// single-line comment

/*
   multi-line comment
*/
```

### Keywords

```
fn       let      mut      return   yield    if       else
while    for      in       break    continue match    struct
enum     trait    impl     import   export   coroutine
```

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores. They are case-sensitive.

```zephyr
let foo = 1;
let _bar = 2;
let camelCase = 3;
let SCREAMING_SNAKE = 4;
```

### Scoping

Zephyr uses **lexical (block) scoping**. A new scope begins with `{` and ends with `}`. Inner scopes can shadow outer bindings.

```zephyr
let x = 10;
{
    let x = 20;   // shadows outer x
    print(x);     // 20
}
print(x);         // 10
```

### Semicolons

Statements are terminated with `;`. Semicolons inside blocks are required.

```zephyr
let a = 1;
let b = 2;
print(a + b);
```

### Imports

```zephyr
import "std/math";                     // default import (all exports)
import "utils" as u;                   // namespace alias
import { sqrt, abs } from "std/math";  // named imports
```

### Exports

```zephyr
export fn greet(name: string) -> void {
    print(f"Hello, {name}!");
}

export struct Point { x: float, y: float }
```

### Type annotations

Type annotations follow the binding with `:`:

```zephyr
let count: int = 0;
fn add(a: int, b: int) -> int { return a + b; }
```

`any` opts a binding out of type checking:

```zephyr
let val: any = 42;
val = "now a string";
```
