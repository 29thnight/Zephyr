# 제네릭 (Generics)

제네릭은 함수와 구조체를 타입에 대해 매개변수화하는 메커니즘을 제공하여, 정적 타입 안전성을 유지하면서 코드 재사용성을 높여줍니다.

## 제네릭 함수

함수는 꺾쇠 괄호 `<>` 내부에 하나 이상의 타입 파라미터를 정의할 수 있습니다.

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}

let a = identity(10);      // T는 int로 결정됨
let b = identity("hi");    // T는 string으로 결정됨
```

## 트레이트 경계 (where 절)

`where` 절을 사용하여 타입 파라미터가 특정 트레이트를 구현한 타입만 허용하도록 제한할 수 있습니다.

```zephyr
trait Printable {
    fn display(self) -> void;
}

fn process<T>(x: T) -> void where T: Printable + Comparable {
    x.display();
}
```

## 제네릭 구조체 (Generic structs)

```zephyr
struct Pair<A, B> {
    first: A,
    second: B,
}

let p = Pair<int, string> { first: 1, second: "one" };
print(p.first);    // 1
print(p.second);   // one
```

## 제네릭 impl 블록

제네릭 구조체에 대한 메서드를 구현할 때에도 제네릭 파라미터를 사용합니다:

```zephyr
impl<A, B> Pair<A, B> {
    fn swap(self) -> Pair<B, A> {
        return Pair { first: self.second, second: self.first };
    }
}
```

## 실전 활용: `Result<T>`

`Result<T>`는 언어 내부에 기본 탑재되어 제공되는 제네릭 열거형(Enum)입니다:

```zephyr
fn read_config(path: string) -> Result<string> {
    // 내부 연산 에러 전파 (?) 처리를 위해 Result 반환
    let content = read_file(path)?;
    return Ok(content);
}

fn main() -> void {
    match read_config("config.toml") {
        Ok(text) => print(text),
        Err(e)   => print(f"Failed: {e}"),
    }
}
```

## 단형화 (Monomorphisation)

Zephyr의 제네릭은 완전히 컴파일 타임에 해결됩니다. 즉, 코드가 호출될 때 전달하는 실제 타입(`identity<int>` 와 `identity<string>` 등)마다 완전히 별개의 바이트코드(Bytecode) 함수가 독립적으로 생성됩니다. 런타임 중에는 타입 소거(Type Erasure)가 일어나지 않으므로 실행 시점 속도 저하가 없습니다.
