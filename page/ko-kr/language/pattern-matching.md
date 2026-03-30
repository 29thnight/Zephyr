# Pattern Matching

정교한 조건 검사와 데이터 언패킹(Destructuring)을 위해 제공되는 `match` 구문입니다. 입력된 값과 일치하는 첫 번째 패턴 블록이 실행됩니다. 

> [!IMPORTANT]
> 망라성 검사 (Exhaustiveness Checking)
> Zephyr 컴파일러는 `match` 구문이 대상 Enum이나 범위가 가질 수 있는 **모든 경우의 수를 완벽하게 다뤘는지 검사**합니다. 만약 누락된 경우가 있다면 컴파일 에러가 발생합니다. 처리하지 않을 나머지는 와일드카드 기호 `_` 로 명시해야 합니다.

## 리터럴과 범위 (Ranges)

숫자, 문자열 등 직접적인 리터럴 일치 여부를 비롯해 범위 연산자 `..=`를 사용한 패턴을 작성할 수 있습니다.

```zephyr
let score = 85;
match score {
    90..=100 => print("A"),
    80..=89  => print("B"),
    70..=79  => print("C"),
    _        => print("F"),
}
```

## 구조 분해 (Destructuring)

열거형(Enum) 내부의 Payload 데이터나, 구조체(Struct), 튜플(Tuple) 내부의 데이터들을 `match` 스코프 내부의 로컬 변수로 즉시 추출할 수 있습니다.

### 열거형 패턴

```zephyr
enum Message {
    Quit,
    Move(int, int),
    Write(string),
}

let msg = Message::Move(10, 20);

match msg {
    Message::Quit        => print("quit"),
    Message::Move(x, y)  => print(f"move to {x},{y}"),
    Message::Write(text) => print(text),
}
```

### 구조체 패턴

선별적으로 특정 필드의 일치 여부만 지정하고(`x: 0`), 나머지는 추출(`y`)하도록 구성할 수 있습니다.
```zephyr
struct Point { x: int, y: int }
let p = Point { x: 0, y: 5 };

match p {
    Point { x: 0, y } => print(f"on y-axis at {y}"),
    Point { x, y: 0 } => print(f"on x-axis at {x}"),
    Point { x, y }    => print(f"at ({x}, {y})"),
}
```

## 가드 조건 (Guard Conditions)

패턴 매칭 성공 여부에 추가적으로 `if` 기반 임의 수식을 요구할 수 있습니다.

```zephyr
let n = 7;
match n {
    x if x % 2 == 0 => print(f"{x} is even"),
    x                => print(f"{x} is odd"),
}
```

## `Result<T>` 오류 핸들링 활용

일반적인 예외 발생(`throw`)을 사용하지 않는 Zephyr 특성 때문에 연산 성공/실패 계통의 언패킹에도 자주 쓰입니다.

```zephyr
match divide(10, 0) {
    Ok(v)    => print(f"성공: {v}"),
    Err(msg) => print(f"실패: {msg}"),
}
```
