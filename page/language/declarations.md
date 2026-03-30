# Declarations & Scope

In Zephyr, all variables must be explicitly bound and initialized before they are parsed by the AST.

## Let & Mut

- `let` declares an immutable binding. The memory slot prevents reassignment after the initial allocation.
- `mut` permits continuous modifications inside the current scope frame.

```zephyr
let max_health = 100;
// max_health = 200; // Throws a compile-time Error

mut current_health = 50;
current_health += 10; // Evaluates successfully
```

## Shadowing

You can reuse the identical variable names. When redefining a bounded target downstream in an inner block scope, it **shadows** the top-level declaration without mutating the original primitive context.

```zephyr
let x = 10;
{
    let x = 20; // Shallow block shadowing
    print(x);   // 20
}
print(x);       // 10
```

## Explicit Type Annotations

The Compiler includes a bidirectional Type Inference Engine under the hood, so explicit type mappings are seldom necessary unless manually resolving ambiguity.

```zephyr
// Derived as 'int' implicitly
let x = 42; 

// Explicitly marked
let y: float = 42.0; 
```

### Casting (Type Coercion)

You can forcibly cast primitive derivations using the `as` structural keyword.

```zephyr
let value = 15;
let forced_ratio = value as float; // Coerces into 15.0

print(forced_ratio / 2.0); // 7.5
```
