# Math & String Utilities

Built-in standard modules for basic mathematical operations and string manipulation.

## `std/math`

A mathematics library providing common numerical and geometric operations.

```zephyr
import "std/math";

print(sqrt(16.0));          // 4
print(abs(-7.5));           // 7.5
print(floor(3.9));          // 3
print(ceil(3.1));           // 4
print(clamp(15.0, 0.0, 10.0)); // 10
print(lerp(0.0, 10.0, 0.5));   // 5
print(sin(PI / 2.0));          // 1
print(cos(0.0));               // 1
```

### Functions

| Function | Signature |
|---|---|
| `sqrt(x)` | `float -> float` |
| `abs(x)` | `float -> float` |
| `floor(x)`, `ceil(x)` | `float -> float` |
| `clamp(x, min, max)` | `float, float, float -> float` |
| `lerp(a, b, t)` | `float, float, float -> float` |
| `sin(x)`, `cos(x)`, `tan(x)` | `float -> float` |
| `pow(base, exp)` | `float, float -> float` |
| `log(x)` | `float -> float` |
| `PI` | **Constant** `float` |

## `std/string`

A collection of functions for processing UTF-8 strings. Since strings in Zephyr are immutable, all functions return a new string or array.

```zephyr
import "std/string";

print(trim("  hello  "));          // "hello" trims whitespace
print(to_upper("hello"));          // "HELLO" uppercase
print(to_lower("WORLD"));          // "world" lowercase
print(replace("foo bar", "bar", "baz")); // "foo baz" replaces substring
print(split("a,b,c", ","));        // ["a", "b", "c"] splits into array
print(starts_with("hello", "he")); // true 
print(ends_with("hello", "lo"));   // true 
```
