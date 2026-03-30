# Types & Data Structures

Zephyr는 정적 타입(Statically typed) 기반 언어입니다. 모든 바인딩은 컴파일 타임에 알려진 타입을 가져야 하지만, 편의성을 위한 타입 추론 및 여러 유용한 자료구조를 기본적으로 제공합니다.

## 기본 타입 (Primitives)

| 타입 | 설명 | 리터럴 예시 |
|---|---|---|
| `int` | 64비트 부호 있는 정수 | `42`, `-7`, `0` |
| `float` | 64비트 IEEE-754 배정밀도 부동소수점 | `3.14`, `-0.5`, `1.0` |
| `bool` | 논리 불리언 | `true`, `false` |
| `string` | 불변(Immutable) UTF-8 문자열 | `"hello"` |
| `void` | 반환값이 없음을 나타냄 (함수 반환 전용) | — |
| `any` | 동적/검사 생략 다형성 타입 | 모든 값 |

### 타입 추론 (Type Inference)

우변이 리터럴이거나 문맥상 명확할 때, `let` 선언 내의 타입은 자동으로 추론됩니다.

```zephyr
let x = 10;       // int
let y = 3.14;     // float
let z = "hi";     // string
let b = false;    // bool
```

### `nil` 과 `any`

`nil`은 값의 부재를 의미합니다. `any` 타입은 컴파일 타임 타입 검사를 비활성화하므로 신중하게 사용해야 합니다.
정수(`int`) 리터럴은 부동소수점(`float`)이 필요한 혼합 연산에서 자동으로 형변환(Widening) 됩니다.

## 문자열 (String)

문자열은 GC에 의해 추적 수집되며, 불변(Immutable) 특성을 보장합니다.

```zephyr
let greeting = "Hello";
let msg = greeting + ", World!";
```

### 문자열 보간 (Interpolation)

`f"..."` 형태를 사용하여 인라인으로 표현식을 포함할 수 있습니다. 어떠한 타입이든 자동으로 문자열로 변환됩니다.

```zephyr
let name = "Zephyr";
let v = 2;
print(f"Welcome to {name} v{v}"); // Welcome to Zephyr v2
```

표준 라이브러리 `std/string`을 임포트하면 `split`, `trim`, `replace`, `to_upper`, `to_lower` 같은 부가적인 문자열 확장 기능을 사용할 수 있습니다. (`len`, `contains`, `starts_with`, `ends_with`는 기본 제공 기능입니다.)

## 배열 (Array)

배열은 순서가 있고 크기가 동적으로 변경되는 컬렉션입니다. `any` 형태로 다양한 타입을 묶을 수도 있습니다.

```zephyr
let nums = [1, 2, 3, 4, 5];
let names = ["Alice", "Bob"];

// 접근 및 수정
print(nums[0]); // 1
nums[1] = 99;

// 요소 추가
push(nums, 6);
print(len(nums)); // 6
```

### 컬렉션 표준 라이브러리 (`std/collections`)

큐(Queue), 맵(Map), 셋(Set) 등이 필요하다면 `std/collections`을 활용하세요:

```zephyr
import { Map, Set, Queue } from "std/collections";

let m = Map::new();
m.set("key", 42);
print(m.get("key"));   // 42
```

## Result\<T\> 와 오류 처리

Zephyr는 예외(Exception)를 던지는 대신 `Result<T>`라는 내장 합태그(Sum type)를 사용하여 안전한 오류 처리를 권장합니다. 타입은 성공 시 `Ok(T)`, 실패 시 `Err(string)`을 담습니다.

```zephyr
fn divide(a: int, b: int) -> Result<int> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}

// match를 이용한 안전한 추출
match divide(10, 2) {
    Ok(v)    => print(f"Result: {v}"),
    Err(msg) => print(f"Error: {msg}"),
}
```

> [!TIP]
> `?` 연산자를 사용하면 `Err` 발생 즉시 호출자에게 조기 반환(Early return)하므로 코드가 간결해집니다.
