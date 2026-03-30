# 배열 (Arrays)

Zephyr의 배열은 순서가 있고 크기가 동적으로 조절되는 요소들의 컬렉션입니다. 별도로 지정하지 않는 한, 요소의 타입은 기본적으로 `any`입니다.

## 배열 리터럴

배열은 대괄호 `[]`를 사용하여 초기화합니다.

```zephyr
let numbers = [1, 2, 3];
let strings = ["a", "b", "c"];
let empty = [];
```

## 접근 및 수정

요소는 0부터 시작하는 인덱스를 통해 접근합니다. 배열을 수정하려면 `mut` 바인딩이 필요합니다.

```zephyr
let a = [10, 20, 30];
let first = a[0]; // 10

mut b = [1, 2, 3];
b[0] = 100;
```

## 길이

배열에 포함된 현재 요소의 개수는 `len()` 함수를 사용하여 가져옵니다.

```zephyr
let count = len([1, 2, 3]); // 3
```

## 끝에 추가 및 배열 연결 (push / concat)

```zephyr
mut arr = [1, 2, 3];
push(arr, 4);
print(arr);   // [1, 2, 3, 4]

let merged = concat([1, 2], [3, 4]);
print(merged);   // [1, 2, 3, 4]
```

## 문자열로 결합 (join)

```zephyr
let words = ["Hello", "World"];
print(join(words, " "));   // "Hello World"
```

## 반복문 (Iterating)

```zephyr
let items = ["a", "b", "c"];
for item in items {
    print(item);
}
```

인덱스와 함께 순회하려면 `range`를 사용합니다:

```zephyr
for i in range(0, len(items)) {
    print(f"{i}: {items[i]}");
}
```

## 표준 자료구조 (`std/collections`)

배열 외에 강타입(Strongly-typed)으로 지정된 맵(Map), 셋(Set), 큐(Queue), 스택(Stack) 등이 필요할 경우 `std/collections` 내장 모듈을 사용합니다:

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";

let m = Map::new();
m.set("key", 42);
print(m.get("key"));   // 42
print(m.has("key"));   // true

let s = Set::new();
s.add(1);
s.add(2);
s.add(1);
print(s.size());       // 2

let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());    // first
```
