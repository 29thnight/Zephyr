# 함수와 클로저 (Functions and Closures)

함수는 Zephyr에서 코드 실행의 기본 단위입니다. 클로저는 자신을 둘러싼 어휘적 환경을 캡처하는 익명 함수입니다.

## 함수 선언 (fn)

전역 함수는 `fn` 키워드를 사용하여 선언합니다. 파라미터와 반환 타입은 반드시 명시적으로 지정해야 합니다.

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}
```

## 익명 클로저

클로저는 변수에 할당거나 인자로 전달할 수 있는 함수 리터럴입니다.

```zephyr
let multiply = fn(a: int, b: int) -> int {
    return a * b;
};

let result = multiply(10, 5); // 50
```

## 변수 캡처 (Capturing Variables)

클로저는 정의된 위치의 주변 스코프에 있는 변수들을 참조(Reference)로 포착(Capture)하여 사용할 수 있습니다.

```zephyr
fn make_adder(n: int) -> fn(int) -> int {
    return fn(x: int) -> int {
        return x + n;   // n을 포착하여 내부에서 사용
    };
}

let add5 = make_adder(5);
print(add5(3));    // 8
print(add5(10));   // 15
```

## 가변 상태의 캡처 (Capturing Mutable State)

클로저는 변수를 참조로 포착하므로, 하나의 `mut` 바인딩을 여러 클로저가 포착하면 서로 상태를 공유하게 됩니다:

```zephyr
fn make_counter() -> fn() -> int {
    mut count = 0;
    return fn() -> int {
        count += 1;
        return count;
    };
}

let counter = make_counter();
print(counter());   // 1
print(counter());   // 2
print(counter());   // 3
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
// step 0
// step 1
// step 2
```
