# 문자열 (Strings)

Zephyr에서 문자열은 불변(Immutable)의 UTF-8 인코딩된 문자 시퀀스입니다.

## 문자열 리터럴 (String Literals)

문자열은 큰따옴표(`"`)를 사용하여 정의합니다.

```zephyr
let s = "Hello, Zephyr";
```

## 문자열 보간 (String Interpolation)

`f-string` 문법을 사용하여 문자열 내부에 표현식을 직접 삽입할 수 있습니다. 중괄호 `{}` 안의 내용은 런타임에 문자열로 변환되어 결합됩니다.

```zephyr
let name = "Zephyr";
let version = 2;
print(f"Welcome to {name} v{version}");
// Welcome to Zephyr v2
```

어떤 타입의 표현식이든 내부적으로 자동 변환되어 문자열 내에 렌더링됩니다:

```zephyr
let x = 3.14;
print(f"pi ≈ {x}");   // pi ≈ 3.14
```

## 문자열 결합 (Concatenation)

`+` 연산자를 사용하여 두 개 이상의 문자열을 하나로 합칠 수 있습니다.

```zephyr
let a = "Hello";
let b = "World";
let c = a + ", " + b + "!";
print(c);   // Hello, World!
```

## `std/string` 모듈

`std/string` 표준 라이브러리를 임포트하면 보다 강력한 문자열 처리 함수들을 사용할 수 있습니다.

```zephyr
import "std/string";
```

| 함수 | 설명 |
|---|---|
| `split(s, sep)` | 구분자(sep)를 기준으로 문자열을 잘라 배열로 반환 |
| `trim(s)` | 문자열 양 끝의 공백 제거 |
| `replace(s, from, to)` | 포함된 특정 단어(`from`)를 새 단어(`to`)로 모두 교체 |
| `to_upper(s)` | 모든 문자를 대문자로 변환 |
| `to_lower(s)` | 모든 문자를 소문자로 변환 |
| `starts_with(s, prefix)` | 문자열이 특정 접두사로 시작하는지 여부 (bool) |
| `ends_with(s, suffix)` | 문자열이 특정 접미사로 끝나는지 여부 (bool) |
| `contains(s, sub)` | 특정 부분 문자열이 포함되어 있는지 여부 (bool) |
| `len(s)` | 문자열의 바이트 길이 반환 |

```zephyr
import "std/string";

let s = "  Hello, World!  ";
print(trim(s));                    // "Hello, World!"
print(to_upper("hello"));          // "HELLO"
print(split("a,b,c", ","));        // ["a", "b", "c"]
print(replace("foo bar", "bar", "baz")); // "foo baz"
```

## 내장(Built-in) 문자열 함수

이 함수들은 별도의 패키지 임포트 없이도 전역 스코프에서 항상 사용할 수 있습니다:

```zephyr
print(len("hello"));                 // 바이트 길이 반환: 5
print(str(42));                      // 숫자를 문자열로 변환: "42"
print(contains("hello", "ell"));     // 포함 여부 검사: true
print(starts_with("hello", "he"));   // 접두사 검사: true
print(ends_with("hello", "lo"));     // 접미사 검사: true
```
