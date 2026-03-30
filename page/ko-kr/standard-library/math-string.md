# Math & String 유틸리티

기본 수학 연산과 문자열 조작을 위한 내장 표준 모듈입니다.

## `std/math`

반복적인 수치 계산과 기하학적 연산을 제공하는 수학 라이브러리입니다.

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

### 함수 원형 정리

| 함수 | 시그니처 (Signature) |
|---|---|
| `sqrt(x)` | `float -> float` |
| `abs(x)` | `float -> float` |
| `floor(x)`, `ceil(x)` | `float -> float` |
| `clamp(x, min, max)` | `float, float, float -> float` |
| `lerp(a, b, t)` | `float, float, float -> float` |
| `sin(x)`, `cos(x)`, `tan(x)` | `float -> float` |
| `pow(base, exp)` | `float, float -> float` |
| `log(x)` | `float -> float` |
| `PI` | **상수 (Constant)** `float` |

## `std/string`

UTF-8 문자열 데이터를 가공하는 함수들의 모음입니다. 문자열의 특성상 내부 데이터는 불변(Immutable)으로 유지되며, 모든 함수는 새로운 문자열이나 배열을 반환합니다.

```zephyr
import "std/string";

print(trim("  hello  "));          // "hello" 공백 자르기
print(to_upper("hello"));          // "HELLO" 대문자
print(to_lower("WORLD"));          // "world" 소문자
print(replace("foo bar", "bar", "baz")); // "foo baz" 부분 단어 치환
print(split("a,b,c", ","));        // ["a", "b", "c"] 배열 변환
print(starts_with("hello", "he")); // true 시작 단어 포함 여부
print(ends_with("hello", "lo"));   // true 끝 단어 포함 여부
```
