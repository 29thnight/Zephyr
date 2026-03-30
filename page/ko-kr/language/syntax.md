# Syntax & Operators

Zephyr의 토큰, 기본 문법 및 연산자는 Rust의 디자인 철학을 따르며, 게임 엔진 스크립팅 환경에 유용한 편의 기능들을 제공합니다.

## 예약 키워드 (Keywords)

```text
fn       let      mut      return   yield    if       else
while    for      in       break    continue match    struct
enum     trait    impl     import   export   coroutine
```

## 식별자 (Identifiers)

식별자는 영문자나 밑줄(`_`)로 시작해야 하며 영문, 숫자, 밑줄을 포함할 수 있습니다. 대소문자를 엄격히 구분합니다.

```zephyr
let foo = 1;
let _bar = 2;
let camelCase = 3;
let SCREAMING_SNAKE = 4;
```

## 주석 (Comments)

```zephyr
// 한 줄 주석 기호입니다.

/*
   여러 줄에 걸친
   주석 기호입니다.
*/
```

## 세미콜론 (Semicolons)

일반적인 구문은 세미콜론(`;`)으로 끝납니다. 중괄호(`{}`)로 둘러싸인 블록 내부에서도 세미콜론 사용이 원칙적으로 요구됩니다.

```zephyr
let a = 1;
let b = 2;
print(a + b);
```

## 연산자 (Operators)

Zephyr는 직관적인 산술, 논리 및 제어 연산자를 제공합니다.

| 분류 | 연산자 목록 |
|---|---|
| **산술 (Arithmetic)** | `+`, `-`, `*`, `/`, `%`, 단항 `-` |
| **비교 (Comparison)** | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| **논리 (Logical)** | `&&` (AND), `||` (OR), `!` (NOT) |
| **할당 (Assignment)**| `=`, `+=`, `-=`, `*=`, `/=` |

> [!NOTE]
> `&&`와 `||` 연산자는 단락 평가(Short-circuit)를 통해 빠른 분기 연산을 보장합니다.

### 옵셔널 체이닝 (`?.`)

`?.` 연산자는 객체나 필드가 `nil`일 때 발생할 수 있는 참조 오류를 방지하고 연산을 `nil`로 단락시킵니다.

```zephyr
let name = user?.profile?.name;
```

### 오류 전파 (`?`)

에러 처리를 간결하게 만들기 위해 `?`를 사용합니다. `Result<T>`를 평가할 때, 결과가 `Err`이면 현재 함수에서 즉시 실행을 멈추고 `Err`를 반환합니다.

```zephyr
fn load() -> Result<string> {
    let content = read_file("data.json")?;
    return Ok(content);
}
```

## 연산자 우선순위

연산자는 다음 순서대로 적용되며 상단일수록 우선순위가 높습니다. 괄호 `()`를 사용해 우선순위를 재지정할 수 있습니다.

1. `!`, 단항 `-`
2. `*`, `/`, `%`
3. `+`, `-`
4. `<`, `<=`, `>`, `>=`
5. `==`, `!=`
6. `&&`
7. `||`
8. `=`, `+=`, `-=`, `*=`, `/=`
