# 타입 시스템 (Type System)

Zephyr는 정적 타입 언어입니다. 모든 바인딩은 컴파일 타임에 알려진 타입을 가지며, 이는 메모리 안전성과 실행 효율성을 보장합니다.

## 기본 타입 (Primitive Types)

| 타입 | 설명 |
|---|---|
| `int` | 64비트 부호 있는 정수. |
| `float` | 64비트 IEEE-754 배정밀도 부동 소수점. |
| `bool` | 부울 값 (`true` 또는 `false`). |
| `string` | 불변 UTF-8 인코딩 문자열. |
| `void` | 값의 부재를 나타내며, 함수의 반환 타입으로만 사용됩니다. |
| `any` | 특정 바인딩에 대한 컴파일 타임 타입 검사를 비활성화합니다. |

## 타입 추론 (Type Inference)

컴파일러는 변수가 리터럴로 초기화될 때 그 타입을 자동으로 추론합니다.

```zephyr
let x = 10;      // int로 추론
let y = 3.14;    // float로 추론
let z = "hello"; // string으로 추론
```

## `nil`

`nil`은 값의 부재를 나타냅니다. `any` 타입에 할당하거나, 향후 지원될 널 안전 타입(`T?`)에서 사용할 수 있습니다.

```zephyr
let x: any = nil;
```

## Result<T> 및 에러 전파

`Result<T>`는 실패할 수 있는 연산을 처리하기 위해 사용되는 내장 열거형입니다.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}
```

### `?` 연산자

`Result<T>`를 반환하는 표현식 뒤에 `?` 연산자를 붙일 수 있습니다. 결과가 `Ok(v)`이면 표현식은 `v`로 평가됩니다. 결과가 `Err(msg)`이면 현재 함수는 즉시 해당 `Err`를 반환합니다.

```zephyr
fn attempt_operation() -> Result<int> {
    let value = perform_risky_task()?; // 작업 실패 시 즉시 Err 반환
    return Ok(value + 1);
}
```

## 혼합 연산 (Mixed Expressions)

정수 리터럴이 부동 소수점 값과 함께 연산에 사용될 때 자동으로 `float`로 확장(Widening)됩니다.

```zephyr
let a = 10;
let b = 2.5;
let c = a + b; // 결과는 12.5 (float)
```

## 복합 타입 (Complex Types)

메모리에 할당되며 가비지 컬렉터의 4단계 라이프사이클 관리(Nursery -> Old 등)를 받는 객체들입니다.

- **Array (배열)**
- **Struct / Enum Instance (구조체 및 열거형 인스턴스)**
- **Function / Closure (함수 및 클로저 참조 객체)**
> [!NOTE] 런타임 타입 검증
> 엄격한 함수 시그니처(`-> int` 등) 및 구조체 초기화, C++ 네이티브 `Host Handle` 매핑 시점에 타입 추론 시스템과 VM이 강력하게 불일치를 감지해 엔진 크래시를 막고 내부 예외(`Result Error`)로 전파합니다.
