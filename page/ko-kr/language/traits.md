# 트레이트 및 제네릭 (Traits & Generics)

타입 간의 공통된 행위를 정의하는 `trait`와, 타입에 구애받지 않고 유연한 코드를 작성할 수 있게 하는 제네릭(`Generics`) 기능을 제공합니다.

## 트레이트 (Trait)

트레이트는 특정 타입이 반드시 구현해야 하는 메서드 시그니처들의 집합(인터페이스)을 정의합니다.

```zephyr
trait Animal {
    fn sound(self) -> string;
    fn name(self) -> string;
}
```

### 트레이트 구현 (`impl`)

특정 타입에 대해 트레이트를 구현하려면 `impl Trait for Type` 구문을 사용합니다. 컴파일러는 트레이트에 정의된 모든 메서드가 구현 블록에 존재하는지 확인(Semacheck)합니다.

```zephyr
struct Dog { breed: string }
struct Cat { indoor: bool }

impl Animal for Dog {
    fn sound(self) -> string { return "Woof"; }
    fn name(self)  -> string { return "Dog"; }
}

impl Animal for Cat {
    fn sound(self) -> string { return "Meow"; }
    fn name(self)  -> string { return "Cat"; }
}
```

### 트레이트 메서드 호출

```zephyr
let d = Dog { breed: "Labrador" };
print(d.sound());   // Woof
print(d.name());    // Dog
```

---

## 다중 트레이트 구현 (Multiple Traits)

하나의 타입이 여러 개의 트레이트를 동시에 구현할 수 있습니다.

```zephyr
trait Drawable {
    fn draw(self) -> void;
}

trait Resizable {
    fn resize(self, factor: float) -> void;
}

struct Sprite { width: float, height: float }

impl Drawable for Sprite {
    fn draw(self) -> void {
        print(f"Sprite({self.width}x{self.height})");
    }
}

impl Resizable for Sprite {
    fn resize(self, factor: float) -> void {
        // 크기 조정 로직
    }
}
```

---

## 트레이트 경계 (Trait Bounds)

`where` 절을 사용하여 제네릭 타입 파라미터가 특정 트레이트를 구현하도록 제한할 수 있습니다.

```zephyr
trait Comparable {
    fn less_than(self, other: Self) -> bool;
}

fn min<T>(a: T, b: T) -> T where T: Comparable {
    if a.less_than(b) { return a; }
    return b;
}
```

---

## 트레이트 없는 impl (impl without a Trait)

`for Trait` 절 없이 사용되는 `impl` 블록은 해당 구조체에 직접 메서드를 추가합니다.

```zephyr
struct Vec2 { x: float, y: float }

impl Vec2 {
    fn length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}
```
