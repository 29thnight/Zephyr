# 트레이트 및 제네릭 (Traits & Generics)

타입 간의 공통된 행위를 정의하는 `trait`와, 타입에 구애받지 않고 유연한 코드를 작성할 수 있게 하는 제네릭(`Generics`) 기능을 제공합니다.

## 트레이트 (Trait)

트레이트는 특정 타입이 반드시 구현해야 하는 메서드 시그니처들의 집합(인터페이스)을 정의합니다.

```zephyr
trait Drawable {
    fn draw(self) -> void;
    fn get_bounds(self) -> Rect;
}
```

### 트레이트 구현 (`impl`)

특정 타입에 대해 트레이트를 구현하려면 `impl Trait for Type` 구문을 사용합니다. 컴파일러는 트레이트에 정의된 모든 메서드가 구현 블록에 존재하는지 확인(Semacheck)합니다.

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

## 제네릭 (Generics)

함수나 구조체를 특정 타입에 종속되지 않는 범용적인 형태로 작성할 수 있습니다. 런타임 캐스팅이 발생하는 것이 아니라, 컴파일 타임에 사용된 타입별로 구체화된 바이트코드(Monomorphisation)를 생성합니다.

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

print(identity(42));        // 42 (int)
print(identity("hello"));   // hello (string)
```

```zephyr
// 제네릭 구조체
struct Pair<A, B> {
    first: A,
    second: B,
}
```

### 제약 조건 (`where`)

제네릭 타입 매개변수가 특정한 트레이트를 구현하도록 `where` 절을 통해 조건을 걸 수 있습니다. 이를 통해 제네릭 타입 `T`가 필요한 인터페이스를 갖추고 있음을 보장합니다.

```zephyr
trait Comparable {
    fn less_than(self, other: Self) -> bool;
}

fn min<T>(a: T, b: T) -> T where T: Comparable {
    if a.less_than(b) { return a; }
    return b;
}
```

`+` 기호를 사용하여 여러 트레이트를 묶어서 제약 조건을 지정할 수도 있습니다. (예: `where T: Printable + Comparable`)

## Self 타입

트레이트 내에서 `Self` 키워드는 해당 트레이트를 실제로 구현하고 있는 현재 타입을 참조하는 데 사용됩니다.

```zephyr
trait Comparable {
    fn is_equal(self, other: Self) -> bool;
}
```

> [!WARNING] 선언적 검증 및 보호
> `check` 환경 단계에서, `impl` 구현부 내에서 원형 `trait` 인터페이스의 필수 시그니처를 하나라도 누락하면 컴파일 즉각 실패 메시지를 엔진에서 뱉어냅니다. 이는 스크립트 작성자의 치명적 실수를 방지합니다.
