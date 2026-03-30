# JSON 및 입출력 (JSON & I/O)

외부 파일 데이터나 인터넷 통신으로 넘어온 객체를 직렬화(Serialization)/구문 분석(Parsing)하고, 다시 디스크 영역에 안전하게 읽고 쓸 수 있는 I/O 유틸리티 모음입니다.

## JSON 역직렬화 및 생성 (`std/json`)

JSON 파일이나 문자열 객체를 Zephyr 구조의 데이터(내부 Dictionary 및 Array)로 변환하거나, 반대 방향으로 처리합니다. 에러 안전성을 위해 결과값은 `Result<T>` Enum 바인딩으로 출력됩니다.

```zephyr
import "std/json";

// parse -> Result<any> 반환
let obj: any = parse("{\"x\": 1, \"y\": 2}");

match obj {
    Ok(v)    => print(stringify(v)),
    Err(msg) => print(f"파싱 에러: {msg}"),
}
```

| 함수 | 시그니처 (Signature) |
|---|---|
| `parse(s)` | `string -> Result<any>` |
| `stringify(v)` | `any -> string` |

## 파일 시스템 접근 (`std/io`)

텍스트 파일에 대한 단발성 읽기 및 쓰기 스크립트를 지원합니다. `read_lines`를 통해 줄 단위 순차 탐색도 손쉽게 구현할 수 있습니다. 시스템 I/O 에러 방지를 위해 이 모듈 역시 `Result<T>` 패턴을 사용합니다.

```zephyr
import "std/io";

// txt 파일 전체 읽어오기
let content = read_file("data.txt");
match content {
    Ok(text) => print(text),
    Err(e)   => print(f"로드 실패: {e}"),
}

// 텍스트 파일 저장하기
write_file("out.txt", "Hello!\n");

// 줄(Line) 단위로 읽고 순회 반복하기
let lines = read_lines("data.txt");
match lines {
    Ok(ls) => {
        // 성공 시 ls(유효한 Iterator 그룹)을 순회
        for line in ls { print(line); }
    },
    Err(e) => print(e),
}
```
