# 제어 흐름 (Control Flow)

Zephyr는 실행 흐름을 제어하기 위한 표준적인 분기문과 반복문 구문을 제공합니다.

## 분기문 (if / else)

조건부 분기는 `if` 및 `else` 키워드를 통해 수행됩니다. 모든 블록에는 중괄호 `{}`가 필수입니다.

```zephyr
if value > 10 {
    print("10보다 큽니다");
} else if value == 10 {
    print("정확히 10입니다");
} else {
    print("10보다 작습니다");
}
```

### if let

`if let` 구문은 패턴 매칭과 조건부 분기를 결합한 형태입니다. 주어진 패턴이 표현식의 결과와 매치될 때만 블록이 실행됩니다.

```zephyr
if let Ok(data) = get_resource() {
    print(data);
}
```

## 반복문 (Loops)

Zephyr는 반복적인 실행을 위해 여러 가지 루프 메커니즘을 지원합니다.

### while

`while` 루프는 지정된 불리언 조건이 `true`인 동안 계속해서 실행됩니다.

```zephyr
let mut i = 0;
while i < 5 {
    print(i);
    i += 1;
}
```

### while let

`if let`과 유사하게, `while let` 루프는 표현식에서 반환된 값이 패턴과 계속해서 매치되는 동안 반복 실행됩니다.

```zephyr
while let Some(msg) = queue.pop() {
    process(msg);
}
```

### for-in

`for-in` 루프는 범위(Range)나 반복자 프로토콜(Iterator Protocol)을 구현한 컬렉션을 순회할 때 사용됩니다.

```zephyr
// 배타적 범위 (Exclusive range)
for i in 0..5 {
    print(i); // 0, 1, 2, 3, 4
}

// 포함적 범위 (Inclusive range)
for i in 0..=5 {
    print(i); // 0, 1, 2, 3, 4, 5
}

// Iterator를 활용한 아이템 순회
for item in values {
  print(item);
}
```

## 루프 및 반환 제어

- `break`: 가장 안쪽 루프를 즉시 탈출합니다.
- `continue`: 현재 반복을 건너뛰고 다음 반복 주기로 넘어갑니다.
- `return`: 속해 있는 함수를 종료하며 특정 값을 호출자에게 반환합니다.

```zephyr
fn sign(x: int) -> int {
    if x > 0 { return  1; }
    if x < 0 { return -1; }
    return 0;
}
```

## 이터레이터 프로토콜 (Iterator Protocol)

다음의 내부 이터레이터 프로토콜 형식을 지원하도록 구현된 타입은 모두 `for-in` 루프에서 자연스럽게 활용이 가능합니다:

```zephyr
trait Iterator {
    fn has_next(self) -> bool;
    fn next(self) -> any;
}
```

`std/collections` 표준 라이브러리의 자료구조인 `Map`, `Set`, `Queue` 들은 기본적으로 이 프로토콜을 내장하고 있습니다.

```zephyr
import { Map } from "std/collections";

let scores = Map::new();
scores.set("Alice", 100);
scores.set("Bob", 95);

for entry in scores {
    print(f"{entry.key}: {entry.value}");
}
```
