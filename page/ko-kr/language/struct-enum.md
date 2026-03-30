# Structs & Enums

프로그램에서 사용하는 복합적인 데이터 구조를 정의합니다.

## 구조체 (Struct)

연관된 여러 값(`Fields`)을 그루핑하는 Product Type입니다. 구조체를 선언하고 인스턴스를 만들 때는 모든 필드를 명시해야 하며, 기본값 기능은 없습니다.

```zephyr
struct Point {
    x: float,
    y: float,
}

let p = Point { x: 3.0, y: 4.0 };
print(p.x);
```

변수가 가변(`mut`)으로 선언되었다면 구조체의 필드도 점(`.`) 표기법을 통해 수정 가능합니다.

```zephyr
mut q = Point { x: 0.0, y: 0.0 };
q.x = 10.0;
```

### 구조체 메서드 (`impl`)

`impl` 블록을 사용해 구조체 전용 메서드를 구현할 수 있습니다. 인스턴스 메서드는 항상 첫 번째 파라미터로 명시적인 `self`를 받아야 합니다.

```zephyr
struct Rect {
    width: float,
    height: float,
}

impl Rect {
    fn area(self) -> float {
        return self.width * self.height;
    }
}

let r = Rect { width: 4.0, height: 3.0 };
print(r.area()); // 12
```

## 열거형 (Enum)

값이 미리 지정한 고정된 배리언트(Variants) 집합 중 하나에 속하는 Sum Type입니다.

```zephyr
enum Direction {
    North,
    South,
    East,
    West,
}

let dir = Direction::North;
```

열거형의 배리언트는 별도의 내부 데이터를 지닐 수도 있습니다. (Payload)

```zephyr
enum Shape {
    Circle(float),            // 반지름
    Rect(float, float),       // 너비, 높이
    Point,
}

let s = Shape::Circle(3.0);
```

이러한 형태의 열거형은 구조 분해(Destructuring) 및 분기 로직 처리를 위해 패턴 매칭(`match`) 문법과 강력한 시너지를 일으킵니다.

### 내장 열거형: `Result<T>`

성공 결과 도는 오류 메시지를 반환하는 `Result<T>` 타입 역시 제네릭과 열거형을 이용해 구현된 Zephyr 내장 에러 응답용 Enum 객체입니다.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}
```
