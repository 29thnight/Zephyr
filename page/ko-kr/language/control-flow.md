# Control Flow (흐름 제어)

코드의 분기와 반복, 함수 종료 등을 제어하기 위한 제어문들을 소개합니다.

## 분기 제어 (`if/else`)

조건식 주변에 괄호(`()`)는 필요하지 않으나, 실행할 블록은 중괄호(`{ }`)로 반드시 둘러싸야 합니다.

```zephyr
let x = 10;

if x > 5 {
    print("big");
} else if x == 5 {
    print("five");
} else {
    print("small");
}
```

## 패턴 매칭 (`match`)

다양한 형태의 패턴 추출 및 분기를 단일 구문으로 처리하는 매우 강력한 기능입니다. 모든 경우를 망라(Exhaustiveness)하거나 와일드카드 `_`를 포함해야 합니다.

```zephyr
let n = 2;

match n {
    1 => print("one"),
    2 => print("two"),
    3 => print("three"),
    _ => print("other"),
}
```

> 패턴 매칭 구문에 대한 구조 분해, 조건 Guard 등 상세한 방법은 **[Pattern Matching](pattern-matching)** 챕터를 참고해 주세요.

## 반복 루프 (`while`)

가장 기초적인 조건 기반 반복 루프입니다.

```zephyr
mut i = 0;
while i < 5 {
    print(i);
    i += 1;
}
```

## 순회 루프 (`for in`)

컬렉션(Array 등)이나 범위를 순회할 때 사용합니다. 내장 Iterator 프로토콜을 구현하는 모든 개체(`Map`, `Set`, `Queue` 등)를 직접 순회할 수 있습니다.

```zephyr
let names = ["Alice", "Bob", "Carol"];
for name in names {
    print(name);
}

// 인덱스를 사용하는 범위 지정 반복
for i in range(0, 5) {
    print(i); // 0, 1, 2, 3, 4
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
