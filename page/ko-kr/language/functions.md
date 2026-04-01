# 함수와 클로저 (Functions and Closures)

함수는 로직을 캡슐화하는 핵심 단위입니다. Zephyr에서 함수는 **일급 객체(First-class citizens)**로 취급되므로, 변수에 할당하거나 다른 함수의 인자로 전달할 수 있습니다.

## 함수 선언 (`fn`)

함수는 `fn` 키워드로 시작하며, 반드시 파라미터의 타입과 반환 타입을 명시해야 합니다.

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}
```

## 반환 값이 없는 함수 (Void Functions)

반환값이 없는 경우 `-> void`를 사용합니다.

```zephyr
fn log(msg: string) -> void {
    print(msg);
}
```

## 재귀 함수 (Recursion)

Zephyr는 재귀 호출을 지원합니다.

```zephyr
fn fibonacci(n: int) -> int {
    if n < 2 { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}
```

> [!NOTE] 
> 기본값 파라미터(Default arguments)나 선택적 포지셔널 인자 기능은 지원하지 않습니다. 모든 파라미터는 호출 시점에 제공되어야 하며, 만약 선택적인 데이터가 필요하다면 `Result<T>` 혹은 `any`를 활용하세요.

## 일급 객체와 익명 함수

함수 자체를 변수에 저장하거나 콜백(Callback) 인자로 넘길 수 있습니다.

```zephyr
fn double(x: int) -> int { return x * 2; }

// 변수에 저장하여 호출
let f = double;
print(f(5)); // 10

// 고차 함수(Higher-order function)
fn apply(func: fn(int) -> int, x: int) -> int {
    return func(x);
}
```

간단한 콜백은 익명 함수 구문을 사용해 인라인으로 작성할 수 있습니다.
```zephyr
let square = fn(x: int) -> int { return x * x; };
```

## 연관 함수 (Associated Functions)

구조체(Struct)에 종속된 스태틱 메서드 또는 생성자는 `impl` 블록 내부에서 선언하며, `TypeName::fn_name()` 형태로 호출합니다.

```zephyr
impl Vec2 {
    fn zero() -> Vec2 {
        return Vec2 { x: 0.0, y: 0.0 };
    }
}

let origin = Vec2::zero();
```

## 클로저 (Closure)

클로저는 자신을 둘러싼 외부 스코프(Enclosing scope)의 변수를 캡처(Capture)할 수 있는 익명 함수입니다.

```zephyr
fn make_adder(n: int) -> fn(int) -> int {
    return fn(x: int) -> int {
        return x + n;   // 스코프 밖의 'n'을 캡처
    };
}
```

Zephyr는 이러한 외부 변수들을 GC가 관리하는 **Upvalue Cell**로 승격(Promotion)시킵니다. 따라서 클로저가 본래의 스코프를 벗어나 생존하더라도, 캡처된 상태를 온전히 유지하고 수정(Mutation)할 수 있습니다.

```zephyr
fn make_counter() -> fn() -> int {
    mut count = 0;
    return fn() -> int {
        count += 1;
        return count;
    };
}

let counter = make_counter();
print(counter()); // 1
print(counter()); // 2
print(counter()); // 3
```

## 콜백으로서의 클로저 (Closures as Callbacks)

클로저는 다른 함수의 인자로 전달되어 로직의 제어권을 역전시키는 콜백으로 자주 사용됩니다:

```zephyr
fn repeat(n: int, f: fn(int) -> void) -> void {
    mut i = 0;
    while i < n {
        f(i);
        i += 1;
    }
}

repeat(3, fn(i: int) -> void {
    print(f"step {i}");
});
```

---

## 제네릭 함수 (Generic Functions)

함수는 타입에 대해 매개변수화될 수 있습니다. 자세한 내용은 [제네릭](./generics.md) 페이지를 참조하세요.

```zephyr
fn identity<T>(x: T) -> T {
    return x;
}
```

---

## 구현 세부 사항 (Implementation)

클로저에서 캡처된 변수들은 GC가 관리하는 **업밸류 셀(Upvalue Cells)**로 승격됩니다. 이를 통해 클로저가 정의된 스코프보다 오래 생존할 수 있습니다.
