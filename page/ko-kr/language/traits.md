# Traits & Generics

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

구조체(Struct)에 트레이트를 구현하려면 `impl Trait for Type` 구문을 사용합니다. 컴파일러는 요구되는 모든 메서드가 빠짐없이 구현되었는지 검증(Semacheck)합니다.

```zephyr
struct Dog { breed: string }

impl Animal for Dog {
    fn sound(self) -> string { return "Woof"; }
    fn name(self)  -> string { return "Dog"; }
}

let d = Dog { breed: "Labrador" };
print(d.sound()); // Woof
```

하나의 타입이 여러 개의 트레이트를 동시에 구현할 수도 있습니다.

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

제네릭 타입 매개변수가 특정한 트레이트를 규정하도록 `where` 절을 통해 조건을 걸 수 있습니다.

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
