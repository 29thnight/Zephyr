# Struct & Enum

Zephyr는 데이터 레코드를 위한 구조체(Structs)와 합 타입(Sum types)을 위한 열거형(Enums)을 사용합니다. 이는 데이터와 상태를 조직화하는 핵심 도구입니다.

## 구조체와 열거형 (Structs and Enums)

Zephyr는 데이터 레코드를 위한 구조체(Structs)와 합 타입(Sum types)을 위한 열거형(Enums)을 사용합니다. 이는 데이터와 상태를 조직화하는 핵심 도구입니다.

## 구조체 (Structs)

`struct`는 명명된 타입 필드들의 집합입니다.

```zephyr
struct Vec2 {
    x: float,
    y: float,
}
```

### 생성 (Construction)

구조체 리터럴을 사용하여 인스턴스를 생성합니다. 모든 필드는 반드시 초기화되어야 합니다.

```zephyr
let pos = Vec2 { x: 10.0, y: 20.0 };

// 필드 단축 초기화 (Field shorthand)
let x = 5.0;
let y = 5.0;
let origin = Vec2 { x, y };
```

### 필드 접근 및 수정

점(.) 표기법을 사용하여 필드에 접근합니다. 값을 수정하려면 `mut` 바인딩이 필요합니다.

```zephyr
mut player_pos = Vec2 { x: 0.0, y: 0.0 };
player_pos.x = 100.0;
print(player_pos.x); // 100
```

### 메서드 (impl)

`impl` 블록을 사용하여 구조체에 동작을 추가할 수 있습니다. 메서드는 `self`를 첫 번째 파라미터로 받습니다.

```zephyr
struct Rect {
    width: float,
    height: float,
}

impl Rect {
    fn area(self) -> float {
        return self.width * self.height;
    }

    fn scale(self, factor: float) -> Rect {
        return Rect {
            width: self.width * factor,
            height: self.height * factor,
        };
    }
}

let r = Rect { width: 10.0, height: 5.0 };
print(r.area()); // 50.0
```

### 중첩 구조체 (Nested Structs)

구조체는 다른 구조체 인스턴스를 필드로 가질 수 있습니다.

```zephyr
struct Circle {
    center: Vec2,
    radius: float,
}

let c = Circle {
    center: Vec2 { x: 0.0, y: 0.0 },
    radius: 10.0,
};
```

---

## 열거형 (Enums)

`enum`은 여러 변체(Variants) 중 하나가 될 수 있는 타입을 정의합니다.

```zephyr
enum Color {
    Red,
    Green,
    Blue,
}

let c = Color::Red;
```

### 페이로드를 가진 변체

각 변체는 연관된 데이터(Payload)를 가질 수 있습니다.

```zephyr
enum Shape {
    Circle(float),          // 반지름
    Rect(float, float),     // 너비, 높이
    Point,                  // 페이로드 없음
}

let s1 = Shape::Circle(5.0);
let s2 = Shape::Rect(10.0, 20.0);
```

### 패턴 매칭 (Pattern Matching)

열거형은 주로 `match` 표현식을 사용하여 해체됩니다.

```zephyr
fn get_area(s: Shape) -> float {
    match s {
        Shape::Circle(r)  => return 3.14 * r * r,
        Shape::Rect(w, h) => return w * h,
        Shape::Point      => return 0.0,
    }
}
```

### Result\<T\>

`Result<T>`는 에러 처리를 위해 사용되는 내장 열거형입니다.

```zephyr
enum Result<T> {
    Ok(T),
    Err(string),
}

fn divide(a: int, b: int) -> Result<int> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}
```
