# 표준 라이브러리 (Standard Library)

Zephyr의 표준 라이브러리는 가벼운 임베딩 환경을 유지하기 위해 핵심 코어가 최소화되어 있으며, 필요한 모듈만 선택적으로 임포트(`import`)하여 사용할 수 있습니다.

## `std/math`

```zephyr
import "std/math";

print(sqrt(16.0));          // 4
print(abs(-7.5));           // 7.5
print(floor(3.9));          // 3
print(ceil(3.1));           // 4
print(clamp(15.0, 0.0, 10.0)); // 10
print(lerp(0.0, 10.0, 0.5));   // 5
print(sin(PI / 2.0));          // 1
print(cos(0.0));               // 1
```

| 함수 | 시그니처 |
|---|---|
| `sqrt(x)` | `float -> float` |
| `abs(x)` | `float -> float` |
| `floor(x)` | `float -> float` |
| `ceil(x)` | `float -> float` |
| `clamp(x, min, max)` | `float, float, float -> float` |
| `lerp(a, b, t)` | `float, float, float -> float` |
| `sin(x)` | `float -> float` |
| `cos(x)` | `float -> float` |
| `tan(x)` | `float -> float` |
| `pow(base, exp)` | `float, float -> float` |
| `log(x)` | `float -> float` |
| `PI` | `float` 상수 |

## `std/string`

```zephyr
import "std/string";

print(trim("  hello  "));          // "hello"
print(to_upper("hello"));          // "HELLO"
print(to_lower("WORLD"));          // "world"
print(replace("foo bar", "bar", "baz")); // "foo baz"
print(split("a,b,c", ","));        // ["a", "b", "c"]
print(starts_with("hello", "he")); // true
print(ends_with("hello", "lo"));   // true
```

## `std/collections`

```zephyr
import { Map, Set, Queue, Stack } from "std/collections";

// 맵 (Map)
let m = Map::new();
m.set("a", 1);
print(m.get("a"));    // 1
print(m.has("a"));    // true
m.delete("a");
print(m.size());      // 0

// 셋 (Set)
let s = Set::new();
s.add(10);
s.add(20);
s.add(10);            // 중복 무시
print(s.size());      // 2
print(s.has(10));     // true

// 큐 (Queue)
let q = Queue::new();
q.enqueue("first");
q.enqueue("second");
print(q.dequeue());   // "first"
print(q.size());      // 1

// 스택 (Stack)
let stk = Stack::new();
stk.push(1);
stk.push(2);
print(stk.pop());     // 2
```

## `std/json`

```zephyr
import "std/json";

let obj: any = parse("{\"x\": 1, \"y\": 2}");
match obj {
    Ok(v)    => print(stringify(v)),
    Err(msg) => print(f"parse error: {msg}"),
}
```

| 함수 | 시그니처 |
|---|---|
| `parse(s)` | `string -> Result<any>` |
| `stringify(v)` | `any -> string` |

## `std/io`

```zephyr
import "std/io";

let content = read_file("data.txt");
match content {
    Ok(text) => print(text),
    Err(e)   => print(f"error: {e}"),
}

write_file("out.txt", "Hello!\n");

let lines = read_lines("data.txt");
match lines {
    Ok(ls) => for line in ls { print(line); },
    Err(e) => print(e),
}
```

## `std/gc`

스크립트 레벨에서 직접 가비지 컬렉터의 런타임을 정밀하게 제어할 수 있습니다:

```zephyr
import "std/gc";

gc.collect_young();    // Minor 컬렉션 (Nursery 수거)
gc.collect();          // Full 컬렉션 (전체 세대 수거)
gc.step(500);          // 증분(Incremental) 수거 (500 µs 예산)
let s = gc.stats();    // 일시정지(Pause) 및 할당 통계를 포함한 Map 반환
```

## `std/profiler`

```zephyr
import "std/profiler";

profiler.start();
// ... 프로파일링 할 코드 ...
profiler.stop();
profiler.report();   // 표준 출력(stdout)으로 샘플링 데이터 출력
```

또는 `--profile` CLI 플래그를 사용하여 전체 실행 과정을 외부에서 프로파일링할 수 있습니다:

```bash
zephyr run --profile mygame.zph
# zephyr_profile.json 파일이 자동 생성됩니다
```
