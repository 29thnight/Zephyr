# Pattern Matching (`match`)

어떤 값에 존재하는 내부 필드나 배열을 투명하게 파악하여, 예외 없이 안전한 로직 처리를 가능하게 만드는 시스템입니다.

## 다양한 패턴 매칭 예시
와일드카드, `OR 바인딩`, `구조체 필드 분해`, `튜플 및 배열 분해`, `가드(조건식)`를 복합적으로 응용할 수 있습니다.

```zephyr
// 열거형 및 구조체 분해 + 가드(if 조건)
match event {
  Event::Hit(Hit { damage, crit: true }) if damage > 5 => damage,
  Event::None | Event::Hit(_) => 0,
}

// 범위(Range) 패턴 매칭
match value {
  0..10 => "low",
  10..=20 => "mid",
  _ => "high",
}

// 튜플(Tuple) 매칭
match point {
  (x, y) => x + y,
}

// 배열 구조 조건 매칭
match pair {
  [lhs, rhs] => lhs + rhs,
  _ => 0,
}
```
