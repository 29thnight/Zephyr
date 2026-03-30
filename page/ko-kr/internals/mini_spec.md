# 미니 스펙 요약 (Mini Spec)

## 목표 (Goals)

- 라이프타임(Lifetime)이나 차용 검사(Borrow checking) 없이 Rust와 유사한 모던 표면 문법 제공
- 게임 플레이 스크립트에 적합한 Lua 수준의 즉각적인 실행 환경 제공
- 명시적인 호스트 바인딩이 곁들어진 손쉬운 C++ 런타임 내장(Embedding)
- 슈퍼 인스트럭션 융합 기능이 탑재된 레지스터 기반 바이트코드 VM 구축 (Release 빌드에선 무조건 바이트코드로만 작동 보장)

## 표면 문법 (Surface Syntax)

### 선언 (Declarations)

| 키워드 | 목적 |
|---|---|
| `fn` | 기본 함수 선언 |
| `let` / `mut` | 불변성 / 가변성 유지 바인딩 변수 선언 |
| `struct` | 새로운 구조체 타입 생성 |
| `enum` | 열거형 선언 |
| `trait` | 뼈대 규격(트레이트) 정의 |
| `impl` | 지정된 타입에 대해 트레이트 메서드 묶음 배정 구현 |
| `import` / `export` | 모듈의 외부 노출 구문 |

### 기본 내장 타입 (Primitive Types)
`int`, `float`, `bool`, `string`, `void`, `any`

### 고급 타입 (Advanced Types)
- 제네릭 인자(Generic parameters): `<T>`
- 제약 조건문(Where clauses): `where T: Comparable`
- 성공과 에러를 포함하는 통합 규격 타입: `Result<T>`
- 초기 에러 반환(리턴) 탈출 연산: `?`
- 안전한 옵셔널 체인 객체 접근 (단락 평가): `?.`

### 구문 제어 루프 (Statements)
`if/else`, `while`, `for in`, `break`, `continue`, `return`, `yield`, `match`

### 수식 및 표현 (Expressions)
- 리터럴 타입, 배열 객체 제어, 멤버 및 인덱스 호출
- 할당: `=`, `+=`, `-=`, `*=`, `/=`
- 문자열 보간 포맷: `f"..."`
- 익명(Lambda) `fn`, 비동기형 `coroutine fn`, `resume`
- 튜플, 열거형 래핑 객체, 범위형 가드 조건이 탑재된 완전성 지향의 `match` 분기
- 관련 내부 요소 접근: `TypeName::method()`

## 모듈 시스템 구조 (Module System)

- `set_package_root()` C++ 함수를 통해 `package.toml`을 스캔하고 스크립트 엔진 내부에 디렉토리 탐색 경로를 자동 구성
- 바이트코드 파일 수명은 모듈 원본의 호스트 OS 파일 수정 날짜(mtime)로 캐시 무효화 여부 추적

## 자료 관리 모델 (Data Model)

- 원시 데이터: `nil`, `bool`, `int`, `float`, `string`
- 런타임 힙 상주 객체: `Array`, 일급 함수 및 Closure, `StructInstance`, `EnumInstance`, 비동기 코루틴 프레임
- 게임용(호스트) 객체는 스크립트 세계로 복사본 직렬화가 아닌, 세대별 체크가 적용된 안전한 `HostHandleToken` 번호 배정만 사용하여 전달
- 코어 글로벌 내장 함수: `print`, `assert`, `len`, `str`, `contains`, `starts_with`, `ends_with`, `push`, `concat`, `join`, `range`

## VM 아키텍처 상세 (VM Architecture)

- 피연산자 연산 스택 오버헤드가 배제된 완전한 레지스터 기반 `R_*` 단일 파이프라인 작동
- 흔한 명령어 패턴 병합 (Superinstruction): `SI_ADD_STORE`, `SI_CMP_JUMP` 등
- 2단계 시멘틱 분석 전처리: 함수 사전 Hoisting 조율 + 트레이트 내부 `impl` 미구현 시 오류 검출
- 가비지 순환 대상 제외, GC 루트 즉결 고정을 통한 문자열 불변 해싱화 체계
- 프로덕션(Release) 모드 성능 강화를 위한 무(Zero)-AST 최적화 전환

## 가비지 컬렉터 설계 (GC)

- 2세대 분류 모델: 갓 태어난 신생아 객체(Nursery) / 오랫동안 살아남은 객체군(Old)
- 젊은 객체를 참조하는 부모만을 탐색하기 위한 비트맵 형식 구조의 Card Table 및 Write Barrier 추적
- 지연된 메모리 찌꺼기 거두기(Lazy sweep) 처리, 메모리 압박 시 Nursery 사이즈 탄력성 제공
- 프레임 드랍(스파이크) 문제 해결을 위한 타임박스(Timebox) 기반 증분 GC 스텝 (`gc_step()`)
- 시각적, 정량적 최적화를 돕는 `start_gc_trace()` / `get_gc_trace_json()` 프로파일 스냅샷
- 배리어 작동 범위: 전역/환경 변수, 구조체 필드 덮어쓰기, 배열 내용 편집, 클로저 내 Upvalue 캡처 조작 시 및 코루틴 슬롯 저장

## 호스트 핸들 수명 체계 (Host Handle Lifetimes)

| 구분 등급 (Class) | 생명 주기 스코프 (Scope) | 바이너리 세이브 상태 (Serializable) |
|---|---|---|
| `Frame` | 현재 단일 함수(콜 프레임) 종료 전 즉시 소멸 | No |
| `Tick` | 매 엔진의 다음 프레임 루프 갱신 전까지 | No |
| `Persistent` | 콜 프레임, 틱 제약 없이 유지되나 다음 게임 씬에선 파기 | No |
| `Stable` | 메모리에 항상 무기한 장기 유지 및 세이브 파일 직렬화 | Yes |

- 디버그 빌드 환경에선 소멸 핸들에 접근 시 강제 Trap 중단을 발생 시키나 플랫폼 릴리즈 시에는 정책 구성에 따라 크래시 억제 Fault 발생 제어
- 바이트코드화 API인 `serialize_value()` 등은 `Stable` 하지 않은 핫 모듈에 대해서 거절 처리
- 세이브 포맷 규격 사용 시 `ZephyrSaveEnvelope` 래퍼 구조 고정 활용 (`schema = "zephyr.save"`, `version = 2` 규격)

## 코어 시스템 라이브러리 (Standard Library)

| 네임스페이스 경로 (Module) | 제공 (Contents) |
|---|---|
| `std/math` | 산술 및 숫자, 삼각함수 유틸 |
| `std/string` | 문자 검색 판별 기능 |
| `std/collections` | 맵 사전, 해시 셋, 큐 |
| `std/json` | JSON 디코더 |
| `std/io` | 디스크 파일 처리 |
| `std/gc` | 수동 가비지 컬렉터 강제 조정 |
| `std/profiler` | 외부 파일 샘플링 및 기록, CLI `--profile` 플래그로 연계 |

## 엔진 임베더 (Host API Surface)

단순한 포인터 제공 형태가 아닌 모든 포인터를 Handle Token으로 방어하며 JSON 추적, C++ 상 코루틴 개입 제어 등 풍부한 관찰 기능을 거대한 매크로 규칙 매핑 의존 없이 간편 함수 형태로 노출합니다.
