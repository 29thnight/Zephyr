# Declarations (선언문)

프로그램의 상태를 관리하고 로직을 캡슐화하기 위한 변수 및 함수의 정의 문법을 소개합니다.

## 변수 선언 (`let`)

변수는 기본적으로 불변(Immutable) 상태로 할당되며, 한번 값이 할당되면 수정할 수 없습니다.

```zephyr
let score = 10;
let hp: int = 100;
```

### 가변 변수 (`mut`)

변경이 필요한(Mutable) 변수는 `mut` 키워드를 결합하여 선언합니다.

```zephyr
mut offset = 0; // 혹은 let mut offset = 0;
offset += 5;
```

## 타입 어노테이션 (Type Annotations)

변수명 뒤에 `:`를 붙여 명시적인 타입 힌팅을 지정할 수 있습니다. 

```zephyr
let count: int = 0;
```

만약 컴파일 타임의 타입 제약을 무시하고 런타임에 동적으로 타입을 변경해야 한다면, `any` 타입을 지정할 수 있습니다.
```zephyr
let val: any = 42;
val = "now a string"; // any 타입이므로 허용됨
```

## 스코프 및 섀도잉 (Scoping & Shadowing)

Zephyr는 통상적인 **블록 스코프(Lexical scoping)** 규칙을 사용합니다. `{` 와 `}`가 만들어내는 영역 내부에서 새 변수를 선언할 수 있으며, 이 때 외부 스코프에 있는 동일한 이름의 식별자를 섀도잉(Shadowing)할 수 있습니다.

```zephyr
let x = 10;
{
    let x = 20;   // 바깥쪽 x를 섀도잉
    print(x);     // 20
}
print(x);         // 10
```

## 함수 선언 (`fn`)

함수는 `fn` 키워드로 시작하며, 반환 타입을 `->` 뒤에 기재합니다.

```zephyr
fn add(a: int, b: int) -> int {
  return a + b;
}
```

### 기타 선언 및 모듈

구조체(`struct`), 열거형(`enum`), 트레이트(`trait`), 제네릭 연동, 코루틴 등의 특수한 선언과 `import` 및 `export` 등은 하위 섹션에서 자세히 다룹니다.
