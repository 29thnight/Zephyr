# Syntax & Lexical

# 문법 (Syntax)

Zephyr는 현대적인 시스템 프로그래밍 언어에서 영감을 받은 중괄호 기반 문법을 사용합니다. 대소문자를 구분하며, 문장 종료를 위해 세미콜론(`;`)이 필요합니다.

## 주석 (Comments)

Zephyr는 단일 행 및 다중 행 주석을 모두 지원합니다.

```zephyr
// 단일 행 주석입니다.

/* 
   다중 행
   주석입니다.
*/
```

## 키워드 (Keywords)

다음 단어들은 예약어이므로 식별자로 사용할 수 없습니다:

| | | | | | | |
|---|---|---|---|---|---|---|
| `fn` | `let` | `mut` | `return` | `yield` | `if` | `else` |
| `while` | `for` | `in` | `break` | `continue` | `match` | `struct` |
| `enum` | `trait` | `impl` | `import` | `export` | `coroutine` | |

## 식별자 (Identifiers)

식별자는 반드시 문자(`a-z`, `A-Z`) 또는 언더스코어(`_`)로 시작해야 합니다. 그 다음 문자부터는 문자, 숫자(`0-9`), 또는 언더스코어를 포함할 수 있습니다.

```zephyr
let variable_name = 10;
let _privateCounter = 0;
let camelCase = true;
```

## 스코프 (Scoping)

Zephyr는 **어휘적 스코프(Lexical Scoping)**를 채택하고 있습니다. 중괄호 `{}`를 사용하여 새로운 스코프를 생성합니다. 블록 내부에서 선언된 변수는 블록 외부에서 접근할 수 없습니다. 내부 스코프는 외부 스코프의 변수를 섀도잉(Shadowing)할 수 있습니다.

```zephyr
let x = 10;
{
    let x = 20; // 외부 x를 섀도잉합니다.
    print(x);   // 20을 출력합니다.
}
print(x);       // 10을 출력합니다.
```

## 세미콜론 (Semicolons)

모든 문장은 세미콜론(`;`)으로 종료되어야 합니다. 이는 변수 선언, 표현식, 블록 내부의 제어 흐름 문장 등을 포함합니다.

```zephyr
let a = 1;
let b = 2;
print(a + b);
```

## 타입 어노테이션 (Type Annotations)

Zephyr는 타입 추론 기능을 제공하지만, `:` 문법을 사용하여 타입을 명시적으로 지정할 수도 있습니다.

```zephyr
let count: int = 0;
fn add(a: int, b: int) -> int {
    return a + b;
}
```

`any` 타입을 사용하면 특정 바인딩에 대한 정적 타입 검사를 우회할 수 있습니다.

```zephyr
let dynamic_val: any = 42;
dynamic_val = "이제 문자열입니다";
```
