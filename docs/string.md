## String

Strings are immutable UTF-8 sequences. String literals use double quotes.

```zephyr
let s = "Hello, World!";
print(s);
```

### Concatenation

Use `+` to concatenate strings:

```zephyr
let a = "Hello";
let b = "World";
let c = a + ", " + b + "!";
print(c);   // Hello, World!
```

### Interpolation

`f"..."` strings embed expressions inline:

```zephyr
let name = "Zephyr";
let version = 2;
print(f"Welcome to {name} v{version}");
// Welcome to Zephyr v2
```

Expressions of any type are automatically converted to their string representation:

```zephyr
let x = 3.14;
print(f"pi ≈ {x}");   // pi ≈ 3.14
```

### std/string module

```zephyr
import "std/string";
```

| Function | Description |
|---|---|
| `split(s, sep)` | Split string by separator → array |
| `trim(s)` | Remove leading/trailing whitespace |
| `replace(s, from, to)` | Replace all occurrences |
| `to_upper(s)` | Uppercase |
| `to_lower(s)` | Lowercase |
| `starts_with(s, prefix)` | bool |
| `ends_with(s, suffix)` | bool |
| `contains(s, sub)` | bool |
| `len(s)` | Length in bytes |

```zephyr
import "std/string";

let s = "  Hello, World!  ";
print(trim(s));                    // Hello, World!
print(to_upper("hello"));          // HELLO
print(split("a,b,c", ","));        // ["a", "b", "c"]
print(replace("foo bar", "bar", "baz")); // foo baz
```

### Built-in string builtins

These are available without any import:

```zephyr
print(len("hello"));          // 5
print(str(42));               // "42"
print(contains("hello", "ell"));     // true
print(starts_with("hello", "he"));   // true
print(ends_with("hello", "lo"));     // true
```
