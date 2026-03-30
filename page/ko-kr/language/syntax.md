# 문법 및 연산자 (Syntax & Operators)

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

Zephyr는 **어휘적 스코프(Lexical Scoping)**를 채택하고 있습니다. 중괄호 `{}`를 사용하여 새로운 스코프를 생성합니다. 블록 내부에서 선언된 변수는 블록 외부에서 접근할 수 없습니다.

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
