# Syntax & Lexical

Zephyr의 토큰 및 기본 문법은 Rust의 디자인 철학을 따릅니다.

## 예약 키워드 (Keywords)

```text
fn, coroutine, yield, resume, let, mut,
if, else, while, for, in, break, continue, return,
struct, enum, match, trait, impl, import, export, as,
true, false, nil
```

## 연산자 및 구분자 (Operators)

```text
+ - * / %
== != < <= > >=
&& || !
= += -= *= /=
. ?. ::
.. ..=
```

## 리터럴 (Literals)

지원하는 기본 리터럴 형태입니다.
- **숫자**: `100`, `3.14`
- **불리언 및 널**: `true`, `false`, `nil`
- **배열**: `[1, 2, 3]`
- **문자열 템플릿**: `f"hp={value}"`
