# 패키지 구조 (Package Layout)

Zephyr 생태계의 패키지 포맷은 복잡도 없는 단순한 트리 구조 레이아웃을 사용합니다:

```text
my_package/
├── package.toml
├── src/
│   ├── lib.zph
│   └── *.zph
└── tests/
    └── *.zph
```

## `package.toml` 구성

```toml
[package]
name = "my_package"
version = "0.1.0"
entry = "src/lib.zph"

[dependencies]
math = "std/math"
utils = "path/to/utils"
```

`entry` 항목은 프로젝트의 진입점(Entry) 모듈을 결정합니다. 호스트 엔진단에서 `ZephyrVM::set_package_root()` API를 호출하면, 런타임은 해당 `package.toml`을 파싱한 뒤 패키지 루트와 진입 모듈 경로를 검색(Search path) 테이블에 추가합니다. 

이후 해당 환경의 다른 파일에서 `import "lib"`를 호출하면 자동으로 `src/lib.zph`를 해석합니다.

## 모듈 탐색 우선순위 (Module Search Order)

임포트 발생 시 다음 순서대로 검색합니다:
1. 해당 임포트 코드를 실행한 파일과 동일한 디렉토리 우선
2. 프로젝트 패키지 루트 경로 (위의 `set_package_root()`로 등록된 위치)
3. 호스트 엔진에서 등록한 모듈 (`register_module()`)
4. 코어에 내장된 `std/` 표준 라이브러리들

## 표준 라이브러리 일람 (Standard Library Modules)

| 임포트 호출명 (Import) | 구성 요소 (Contents) |
|---|---|
| `import "std/math"` | `sin`, `cos`, `sqrt`, `abs`, `floor`, `ceil`, `clamp`, `lerp` |
| `import "std/string"` | `split`, `trim`, `replace`, `to_upper`, `to_lower`, `format` |
| `import "std/collections"` | `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>` |
| `import "std/json"` | `parse(s: string) -> Result<any>`, `stringify(v: any) -> string` |
| `import "std/io"` | `read_file`, `write_file`, `read_lines` |
| `import "std/gc"` | `collect()`, `collect_young()`, `step()`, `stats()` |
| `import "std/profiler"` | `start()`, `stop()`, `report()` |

## 모듈 바이트코드 캐싱 체계 (Module Bytecode Caching)

컴파일 비용 단축을 위해 모듈은 소스 파일의 `mtime`(마지막 수정 날짜) 기준으로 캐시화됩니다. 한 번 컴파일되어 파일 로드 이벤트가 발생하면 런타임은 그 즉시 동일 폴더에 `.zphc` 캐시 파일을 배출합니다. 이후 실행부터는 OS상 디스크 날짜를 비교하여 파일 변조가 없다면 파싱 단계를 철저히 무시하고 캐시된 바이너리 바이트코드를 즉각 주입합니다.

이러한 캐시 파일들은 기본적으로 원본 소스가 위치한 디렉토리와 같이 배출되지만 `ZEPHYR_CACHE_DIR` 환경변수를 설정하여 한 곳에 몰아서 숨길 수도 있습니다.
