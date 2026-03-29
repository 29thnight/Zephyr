# Syntax & Lexical

Zephyr's lexical tokens and basic grammar closely follow Rust's design philosophy.

## Keywords

```text
fn, coroutine, yield, resume, let, mut,
if, else, while, for, in, break, continue, return,
struct, enum, match, trait, impl, import, export, as,
true, false, nil
```

## Operators & Delimiters

```text
+ - * / %
== != < <= > >=
&& || !
= += -= *= /=
. ?. ::
.. ..=
```

## Literals

Supported literal forms natively tokenized by Zephyr:
- **Numbers**: `100`, `3.14`
- **Boolean & Null**: `true`, `false`, `nil`
- **Arrays**: `[1, 2, 3]`
- **Format string (f-string)**: `f"hp={value}"`
