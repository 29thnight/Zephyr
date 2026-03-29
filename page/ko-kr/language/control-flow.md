# Control Flow (흐름 제어)

게임 루프를 제어하기 위한 기본적인 분기와 반복 외에도, 패턴 기반의 강력한 `let` 통합 문법 체계를 도입했습니다.

## 분기 제어 (`if`, `if let`)
```zephyr
if value > 0 {
  print("positive");
} else {
  print("non-negative");
}

// 패턴 매칭 추출이 곧바로 분기 조건으로 전환됩니다
if let Event::Hit(Hit { damage, crit: true }) = event {
  print(damage);
}
```

## 반복 제어 (`while`, `while let`, `for-in`)
```zephyr
// Iterator 방식의 Next Pair 추출 반복 루프
while let [name, hp] = next_pair() {
  print(name);
}

// Iterator를 활용한 아이템 순회
for item in values {
  print(item);
}

// 미만 범위
for i in 0..n {
  print(i);
}

// 이하 (포함) 범위
for i in 0..=n {
  print(i);
}
```
