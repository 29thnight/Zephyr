# 데이터 자료구조 (Collections)

게임 및 애플리케이션 상태를 다루기 위해 필수적으로 제공되는 고급 자료구조(`Map`, `Set`, `Queue`, `Stack`)의 묶음입니다.

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";
```

<div class="custom-features-wrapper">
  <h2>지원 자료구조 종류</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>Map (딕셔너리)</h3>
      <p>고유한 키(`Key`)와 값(`Value`)의 짝을 매칭시켜 관리합니다. 내부적으로 O(1)에 근접하는 해시 테이블 로직으로 동작합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>Set (집합)</h3>
      <p>중복된 데이터 저장을 방지하는 컬렉션입니다. 맵과 동일하게 해시 테이블 기반으로 구동되어 빠른 탐색을 보장합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>Queue & Stack (비선형 배열)</h3>
      <p><code>Queue</code>는 먼저 도착한 값이 가장 먼저 나가는 선입선출(FIFO) 대기열이며, <code>Stack</code>은 후입선출(LIFO) 모델로 동작합니다.</p>
    </div>
  </div>
</div>

## 활용 예제 모음

### Map 활용
```zephyr
let m = Map::new();
m.set("score", 100);

print(m.get("score")); // 100
print(m.has("score")); // true (존재 여부)

m.delete("score");     
print(m.size());       // 0
```

### Set 활용
```zephyr
let s = Set::new();
s.add(10);
s.add(20);
s.add(10);             // 중복 무시됨

print(s.size());       // 2
print(s.has(10));      // true 
```

### Queue & Stack 활용
```zephyr
// Queue (FIFO - 선입선출)
let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());    // "first" 반환됨
print(q.size());       // 1

// Stack (LIFO - 후입선출)
let stk = Stack::new();
stk.push(1);
stk.push(2);
print(stk.pop());      // 2 반환됨
```
