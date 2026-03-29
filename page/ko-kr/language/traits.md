# Traits & Impl

다형성과 인터페이스 기반 설계를 제공하기 위한 기능입니다.

## 역할 선언 (`trait`)
특정 타입들이 반드시 구현해야 하는 고유의 동작(메서드 규격)을 선언합니다. C++의 순수 가상 함수와 비슷한 역할을 담당합니다.

```zephyr
trait Drawable {
  fn draw(self) -> int;
}
```

## 기능 구현체 (`impl`)
정의된 구조체나 열거형 객체의 멤버 함수, 또는 트레이트(Trait)에 종속되는 구체적인 로직을 주입합니다.

```zephyr
struct Player { x: int, y: int }

impl Drawable for Player {
  fn draw(self) -> int {
    return self.x + self.y;
  }
}
```

> [!WARNING] 선언적 검증 및 보호
> 코드베이스의 `check` 환경 단계에서, `impl` 구현부 내에서 원형 `trait` 인터페이스의 필수 시그니처를 하나라도 누락하면 컴파일 즉각 실패 메시지를 엔진에서 뱉어냅니다. 이는 스크립트 작성자의 치명적 실수를 방지합니다.
