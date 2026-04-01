# 현재 구현 상태 (Current State)

## 프로젝트 구조

```
src/
├── zephyr.cpp           VM public API 외부 노출용 진입점
├── zephyr_parser.cpp    Lexer + Parser + Runtime::parse_source()
├── zephyr_gc_impl.cpp   세대별 GC 구현부 (Nursery, Old-gen, Card table, Write barrier)
├── zephyr_internal.hpp  내부 공유 타입, 매크로 정의
├── zephyr_lexer.hpp     어휘 분석기 클래스
├── zephyr_types.hpp     GC 객체 타입, 값(NaN-box) 표현, 호스트 핸들 구조체
└── zephyr_compiler.hpp  Bytecode 명령어, IR, 바이트코드 컴파일러 로직

cli/
├── main.cpp             run / check / repl / stats / dump-bytecode / bench / lsp / dap
├── lsp_server.cpp       LSP v0.2.0: 커서 오버 타입추론(hover), 자동완성, 정의 찾기 등
└── dap_server.cpp       DAP 디버깅 어댑터 (중단점/스텝오버)

include/zephyr/api.hpp   C++ 게임 엔진 연동용 Public API 헤더
bench/                   벤치마크 테스트 자동화 및 JSON 결과물 경로
tests/                   언어 레벨 단위 테스트 (lexer, compiler, VM, GC, host, perf, corpus)
samples/engine_sample/   게임 엔진 호스트 임베딩 예시 코드
std/                     표준 라이브러리 인터페이스 (.zph modules)
editors/vscode-zephyr/   VS Code 플러그인 확장 기능
```

## 빌드 시스템

| 시스템 | 상태 |
|---|---|
| Visual Studio 18 (`Zephyr.sln`) | ✅ 경고 없이 모든 타겟 컴파일 매끄럽게 통과 |
| CMake (`CMakeLists.txt`) | ✅ MSVC 및 GCC 크로스 컴파일 호환 |

컴파일 플래그: `/utf-8 /bigobj /permissive-` (MSVC), `-Wall -Wextra` (GCC/Clang)

## 구현된 언어 기능

| 카테고리 | 특징 / 지원 문법 |
|---|---|
| 선언자 | `fn`, `let`, `mut`, `struct`, `enum`, `trait`, `impl`, `import`, `export` |
| 기본 데이터 타입 | `int`, `float`, `bool`, `string`, `void`, `any` |
| 고급 타입 모델 | `Result<T>`, 다형성 제네릭 `<T>`, `where T: Trait` 제약 절 |
| 흐름 제어 | `if/else`, `while`, `for in`, `break`, `continue`, `return`, `yield`, `match` |
| 단축 연산 | `+=`, `-=`, `*=`, `/=`, `?` (에러 전파 매크로), `?.` (옵셔널 체이닝 무시) |
| 스트링 서식 | `f"..."` 내부 표현식 보간(Interpolation) |
| 코루틴 | `coroutine fn` 키워드, `yield`, `resume`, `.done`, `.suspended` 속성 |
| 패턴 매칭 유추 | 구조체 / 튜플 / 열거 타입 / 범위 / Guard 조합의 분기 검증 (Exhaustiveness checking) |
| 연계 함수 | 구조체나 열거형 내 네임스페이스 격의 `TypeName::method()` 호출 |
| 모듈 체계 | 이름 기반의 외부 모듈 임포트 / 파일 분할 연동 / `set_package_root()` C++ 설정 |
| 시스템 모듈 | `std/math`, `std/string`, `std/collections`, `std/json`, `std/io`, `std/gc`, `std/profiler` |

## VM 아키텍처 지원부

- Register 단위 바이트코드 (스택 구조 아님) + 슈퍼인스트럭션 융합
- 256개 이상의 로컬 변수 포용 처리 (Spill fallback - `R_SPILL_LOAD / STORE`)
- 가상 레지스터 최적 할당기: Live Range 분석과 Copy propagation을 통한 낭비 최소화
- 2단계 시멘틱 분석 검증기: 상위 단위 Hoisting 처리 및 트레이트 규격 만족 심리
- 빠른 해싱을 위한 불변 문자열의 글로벌 관리 (String interning / GC root 고정)
- 모듈 바이트코드 바이너리 전처리 캐싱 (`.zphc` - 마지막 수정 시간 검사 방식)

### 슈퍼인스트럭션 목록

| Opcode | 설명 |
|---|---|
| `R_SI_ADD/SUB/MUL_STORE` | 연산 + 목적지 이동 |
| `R_SI_CMP_JUMP_FALSE` | 비교 + 조건부 점프 |
| `R_SI_CMPI_JUMP_FALSE` | 즉시값 비교 + 조건부 점프 |
| `R_ADDI_JUMP` | 증가 + 무조건 점프 |
| `R_SI_ADDI_CMPI_LT_JUMP` | 증가 + 상한 비교 + 조건부 점프 (루프 back-edge) |
| `R_SI_MODI_ADD_STORE` | `dst = accum + (src % imm)` |
| `R_SI_LOOP_STEP` | 루프 한 스텝 전체: `accum += iter%div; iter += step; if iter < limit goto body` |

### 인라인 캐시 (IC)

- **R_BUILD_STRUCT IC**: 첫 번째 실행 후 `StructTypeObject*`를 캐시. 이후 호출은 타입 탐색·문자열 비교 없이 객체를 직접 할당합니다.
- **StructTypeObject::cached_shape**: Shape 계산(벡터 할당 + 해시맵 조회)을 첫 번째 인스턴스 생성 시 1회로 제한합니다.

## GC (가비지 컬렉터)

- 2가지의 명확히 구분된 세대 관리 모델: Nursery(어린 세대) + Old Generation(과거 세대)
- 카드 테이블 비트맵 캐시 메모리 탑재 (Old 객체가 Young 객체를 참조할 때 발생하는 Write barrier 감시망 운용)
- 무거운 정지(Freeze) 현상 방지를 위해 일하기 전 시간을 정해두고 멈추는 Incremental Budget (`gc_step()`) 스텝 쪼개기 지원.
- 분석용 프로파일 파일 JSON(`zephyr_profile.json`) 내보내기

## 호스트(C++) 연동 API 지원단

- 오버헤드 없이 식별 ID + 세대 스탬프로 엔진 객체를 역방향 검증하는 빠른 바인딩 핸들. 수명 주기에 따라 4가지(`Frame`, `Tick`, `Persistent`, `Stable`) 방식 지원
- `ZephyrClassBinder<T>` 클래스로 C++ 엔진 소스와의 래핑 최소화
- 자동 세이브 직렬화 대응 엔진: `ZephyrSaveEnvelope` 바이너리 스냅샷 및 복구
- 호스트의 C++ 로직 상에서 중간에 개입 가능한 비동기 코루틴 생명주기 API (`query_coroutine()`, `spawn_coroutine()`)

## 최신 벤치마크 결과 (5/5 게이트 승인)

| 항목 (Case) | 평균 소요 | Lua 5.5 | 비율 | 비고 |
|---|---|---|---|---|
| module_import | 838 µs | — | — | 단순 명령어 구조 |
| hot_arithmetic_loop | ~420 µs | 394 µs | 1.07× | R_SI_LOOP_STEP (1 op/iter) |
| array_object_churn | ~1,050 µs | 1,909 µs | **0.55×** ✓ | R_BUILD_STRUCT IC |
| host_handle_entity | ~224 µs | 303 µs | **0.74×** ✓ | — |
| coroutine_yield_resume | ~220 µs | 923 µs | **0.24×** ✓ | — |
| serialization_export | 26.5 µs | — | — | — |
