# Functions & Closures

코드 조각을 패키징하는 단위입니다. C++ 호스트 바인딩을 통해 내부 로직에서 콜백(Callback)이나 핸들러(Handler)의 수단으로 손쉽게 활용할 수 있습니다.

## 함수 선언 (`fn`)
명시적으로 입력과 출력부의 시그니처 힌팅을 지정합니다.

```zephyr
fn calculate_damage(attack: int, defense: int) -> int {
  return attack - defense;
}
```

## 익명 함수 (Lambda / Closure)
한 번만 실행하고 버리거나 이벤트 핸들러 주입 등을 처리할 목적으로 변수에 할당하여 자유롭게 활용할 수 있는 클로저 패턴입니다.

```zephyr
let increment = fn(a: int) -> int {
    return a + 1;
};

// ... 호스트 C++ 엔진으로 `increment` 전달
```
