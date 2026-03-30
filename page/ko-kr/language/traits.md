# Traits & Impl

다형성과 인터페이스 기반 설계를 제공하기 위한 기능입니다.

## 역할 선언 (`trait`)
특정 타입들이 반드시 구현해야 하는 고유의 동작(메서드 규격)을 선언합니다. C++의 순수 가상 함수와 비슷한 역할을 담당합니다.

```zephyr
trait Drawable {
    fn draw(self) -> void;
    fn get_bounds(self) -> Rect;
}
```

## 트레이트 구현

특정 타입에 대해 트레이트를 구현하려면 `impl Trait for Type` 구문을 사용합니다. 컴파일러는 트레이트에 정의된 모든 메서드가 구현 블록에 존재하는지 확인합니다.

```zephyr
struct Sprite {
    id: int,
    pos: Vec2,
}

impl Drawable for Sprite {
    fn draw(self) -> void {
        render_internal(self.id, self.pos);
    }

    fn get_bounds(self) -> Rect {
        return Rect { x: self.pos.x, y: self.pos.y, w: 32.0, h: 32.0 };
    }
}
```

## 트레이트 경계 (Trait Bounds)

제네릭 타입 파라미터에 `where` 절을 사용하여 특정 트레이트를 구현해야 한다는 제약을 걸 수 있습니다. 이를 통해 제네릭 타입 `T`가 필요한 인터페이스를 갖추고 있음을 보장합니다.

```zephyr
fn render_all<T>(items: Array<T>) -> void where T: Drawable {
    for item in items {
        item.draw();
    }
}
```

## 다중 트레이트 구현 (Multiple Traits)

하나의 타입은 필요한 기능적 요구사항을 충족하기 위해 여러 개의 트레이트를 동시에 구현할 수 있습니다.

```zephyr
trait Scalable {
    fn scale(self, factor: float) -> void;
}

impl Scalable for Sprite {
    fn scale(self, factor: float) -> void {
        // 크기 조절 로직
    }
}

// Sprite는 Drawable과 Scalable 트레이트를 모두 구현한 상태가 됩니다.
```

## Self 타입

트레이트 내에서 `Self` 키워드는 해당 트레이트를 실제로 구현하고 있는 현재 타입을 참조하는 데 사용됩니다.

```zephyr
trait Comparable {
    fn is_equal(self, other: Self) -> bool;
}
```

> [!WARNING] 선언적 검증 및 보호
> `check` 환경 단계에서, `impl` 구현부 내에서 원형 `trait` 인터페이스의 필수 시그니처를 하나라도 누락하면 컴파일 즉각 실패 메시지를 엔진에서 뱉어냅니다. 이는 스크립트 작성자의 치명적 실수를 방지합니다.
