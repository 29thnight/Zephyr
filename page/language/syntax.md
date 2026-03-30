# Syntax & Lexical

Zephyr’s syntax builds heavily on **Rust** and modern frameworks, ensuring low mental overhead for anyone used to C++.

## Comments

Double slashes denote single-line comments. Multi-line comments are not supported natively to encourage single-line documentation chains.

```zephyr
// This is a comment.
```

## Reserved Keywords

The language restricts several keywords intrinsic to the interpreter:
`fn`, `let`, `mut`, `if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `struct`, `enum`, `impl`, `trait`, `where`, `match`, `yield`, `coroutine`, `import`, `export`, `as`, `from`, `true`, `false`, `nil`

## Identifiers & Casing Conventions

Variables, function names, and method signatures MUST stick to typical lowercase alphanumeric sequences alongside underscores (e.g. `foo_bar`).
However, Structures (`struct`), Enums (`enum`), and Traits (`trait`) require PascalCase (`MyStruct`).

## Operators

Zephyr handles arithmetic with logical consistency. 

### Core Arithmetic & Assignment

```zephyr
a + b   // Addition
a - b   // Subtraction
a * b   // Multiplication
a / b   // Division
a % b   // Modulo

// Compound Assignments
a += 1;
a -= 2;
```

### Relational & Logical 

```zephyr
a == b  // Equality
a != b  // Inequality
a > b   
a < b   
a >= b  
a <= b  

a && b  // Logical AND
a || b  // Logical OR
!a      // Logical NOT
```

### Advanced Dispatch Operators

The language supports sophisticated query selectors specifically geared towards nullable variables and deep referencing.

- **`?.` (Optional Chaining)** : Safely inspects properties of an object. If the object evaluates to `nil`, it bypasses the exception and returns `nil`.
- **`?` (Error Propagation)** : Validates `Result<T>` Enum structures. If the value comprises `Err(e)`, the surrounding function immediately returns the error; on an `Ok(v)`, it extracts `v`.
