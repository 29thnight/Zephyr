# Declarations (선언문)

프로그램을 다루기 위한 변수 및 함수의 정의 문법을 소개합니다.

## 식별자 및 변수 선언 (`let`)

변수는 기본적으로 불변(Immutable) 상태로 할당되며, 필요시 타입 힌팅을 지정할 수 있습니다.
```zephyr
let score = 10;
let hp: int = 100;
```

변경이 가능한(Mutable) 변수는 `mut` 키워드를 수반하여 정의합니다.
```zephyr
let mut offset = 0;
offset += 5;
```

## 함수 선언 (`fn`)

로직을 캡슐화하는 핵심 바운더리입니다.
```zephyr
fn add(a: int, b: int) -> int {
  return a + b;
}
```

자세한 함수 클로저, 구조체(Struct), 열거형(Enum), 코루틴 객체의 선언 방법은 좌측 메뉴바의 개별 심층 섹션을 참조해주시기 바랍니다.
