# 언어 아키텍처 (Language Architecture)

이 문서는 컴파일 파이프라인, 런타임 가상머신(VM), 타입 시스템, 가비지 컬렉터(GC), 임베딩 API 및 지원 인프라를 포함하여 Zephyr 프로그래밍 언어 구현부의 기술적인 아키텍처를 상세히 설명합니다.

## 1. 개요 (High-Level Overview)

Zephyr는 C++20으로 작성된 정적 타입 형식의 임베더블(Embeddable) 스크립팅 언어입니다. 게임 엔진 스크립팅을 대상으로 하며, Lua 수준의 단순한 임베딩 편의성과 Rust 스타일의 모던 문법을 함께 제공합니다 (단, 복잡한 라이프타임(Lifetime)이나 빌림 검사(Borrow checking) 기능은 생략함).

**주요 설계 원칙:**
- Register 기반 바이트코드 VM과 슈퍼인스트럭션(Superinstruction) 융합
- 비트맵 카드 테이블(Bitmap card table) 방식의 세대별(Generational) GC
- C++ ↔ 스크립트 간 안전한 값 교환을 보장하는 세대 검증형 호스트 핸들
- 핵심 언어 차원에서 네이티브로 지원되는 코루틴(Coroutines)

## 2. 컴파일 파이프라인 (Compilation Pipeline)

### 2.1 렉서 (Lexer)
어휘 분석기(Lexer)는 UTF-8 원본 소스 코드를 단조 연속된(Flat) `Token` 스트림으로 변환합니다. 모든 토큰은 에러 리포팅을 위한 `Span` 정보(위치와 길이)를 내포하고 있습니다.

### 2.2 파서 (Parser)
파서는 추상 구문 트리(AST)를 생성하는 수작업형 재귀 하향(Recursive-descent) 파서입니다.
- 연산자 우선순위 클라이밍을 통한 Pratt(TDOP) 표현식 파싱
- 제네릭 인자 `<T, U>` 및 where 제약 조건 파싱
- `match` 구문의 완전성 확인(Exhaustiveness) 힌트를 병합

### 2.3 의미 분석 패스 1 (Semantic Analysis 1)
우선적으로 모든 최상위 레벨의 `fn`, `struct`, `enum`, `trait`, `impl` 선언문을 모듈 내 심볼 테이블로 미리 올려(Hoisting) 향후 상호 참조가 깔끔하게 해결되도록 준비합니다.

### 2.4 의미 분석 패스 2 (Semantic Analysis 2)
- 모든 식별자(Identifiers)를 심볼 테이블을 기반으로 실제 주소에 매핑 (Resolve)
- `impl` 블록이 요구되는 트레이트 메서드를 온전히 만족하는지 검사
- 클로저의 외부 변수 캡처(Upvalue) 대상 탐지
- 타입을 생략한 `let x = <literal>` 구문에 대한 타입 추론

### 2.5 코드 생성 (Code Generation)
컴파일러는 분석 및 매핑이 완료된 AST를 순회(Walk)하여 무제한 가상 레지스터(Virtual Registers)를 사용하는 중간 언어(IR)를 방출합니다.

### 2.6 레지스터 할당 (Register Allocator)
1. 모든 가상 레지스터의 생존 구간(Live intervals) 계산
2. 선형 스캔(Linear-scan) 할당을 통한 가상 → 물리(0~255 범위) 레지스터 매핑
3. 중복되는 이동 명령들을 복사 전파(Copy propagation)로 제거
4. 레지스터 공간(255개)을 초과할 시 `R_SPILL_STORE` / `R_SPILL_LOAD` 짝 방출

### 2.7 슈퍼인스트럭션 융합 (Superinstruction Fusion)

레지스터 할당 후 `optimize_register_bytecode()` 내 peephole 패스가 `while(changed)` 루프로 인접 및 비인접 패턴을 탐지하여 단일 opcode로 융합합니다.

#### 인접 패턴

| 패턴 | 융합 결과 | 설명 |
|---|---|---|
| `R_ADD/SUB/MUL` + `R_MOVE(dst, result)` | `R_SI_ADD/SUB/MUL_STORE` | 연산 + 목적지 이동 |
| `R_MODI(tmp,src,imm)` + `R_MOVE(dst,tmp)` | `R_MODI(dst,src,imm)` | 임시 레지스터 제거 |
| `R_ADDI(dst,src,imm)` + `R_JUMP(target)` | `R_ADDI_JUMP` | 증가 + 무조건 점프 |
| `R_CMP*` + `R_JUMP_IF_FALSE` | `R_SI_CMP_JUMP_FALSE` | 비교 + 조건부 점프 |
| `R_LOAD_INT(tmp,imm)` + `R_SI_CMP_JUMP_FALSE` | `R_SI_CMPI_JUMP_FALSE` | 즉시값 비교 + 조건부 점프 |
| `R_MODI(tmp,src,imm)` + `R_SI_ADD_STORE(dst,acc,tmp)` | `R_SI_MODI_ADD_STORE` | `dst = acc + (src % imm)` |

#### 비인접 루프 패턴

| 패턴 | 융합 결과 | 설명 |
|---|---|---|
| `R_ADDI_JUMP(reg,+N)` → target: `R_SI_CMPI_JUMP_FALSE(reg,limit)` | `R_SI_ADDI_CMPI_LT_JUMP` | 루프 증가 + 상한 검사 |
| `R_SI_MODI_ADD_STORE(acc,acc,iter,div)` + `R_SI_ADDI_CMPI_LT_JUMP(iter,step,limit)` | `R_SI_LOOP_STEP` | 루프 한 스텝 전체를 1 opcode로 |

#### R_SI_LOOP_STEP 인코딩

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

| 필드 | 역할 |
|---|---|
| `dst` (uint8) | 누적 레지스터 (accum) |
| `src1` (uint8, int8 재해석) | 반복 증가 폭 (step) |
| `src2` (uint8) | 루프 카운터 레지스터 (iter) |
| `operand_a` (uint8) | 나머지 제수 (div, 1~255) |
| `ic_slot[15:0]` (uint16) | 루프 본문 시작 주소 |
| `ic_slot[31:16]` (int16) | 루프 종료 조건 상한 (limit) |

## 3. 런타임 가상머신 (Runtime Virtual Machine)

### 3.1 VM 구조 (VM Structure)
인터프리터 루프는 현재 청크를 계속해서 가져와(Decode), `switch` 방식을 통해 실행(Execute)하는 전형적인 구조입니다. 각각의 콜 프레임(Call Frame)은 다음을 가집니다:
- 실행 중인 `BytecodeChunk` 포인터
- 프로그램 카운터 (PC)
- 현재 프레임 값 스택에 대한 Base 레지스터 오프셋
- 클로저인 경우 소속된 Upvalue 배열 참조 주소

### 3.2 명령어 집합 (Instruction Set)

모든 opcode는 `R_` 접두사를 사용합니다.

| Opcode | 설명 |
|---|---|
| `R_LOAD_CONST` / `R_LOAD_INT` | 상수 또는 즉시 정수 로드 |
| `R_MOVE` | 레지스터 복사 |
| `R_ADD/SUB/MUL/DIV` | 사칙 연산 |
| `R_ADDI/MODI` | 즉시값 연산 |
| `R_CMP_EQ/LT/LE` | 비교 → bool 레지스터 |
| `R_JUMP/R_JUMP_IF_FALSE` | 무조건/조건부 분기 |
| `R_CALL` / `R_RETURN` | 함수 호출/반환 |
| `R_YIELD` / `R_RESUME` | 코루틴 중단/재개 |
| `R_GET_FIELD/R_SET_FIELD` | 구조체 필드 접근 |
| `R_BUILD_STRUCT` | 구조체 리터럴 할당 및 초기화 |
| `R_SPILL_LOAD/R_SPILL_STORE` | 256개 초과 로컬 변수용 힙 스필 |
| `R_SI_ADD/SUB/MUL_STORE` | 연산 + 저장 융합 |
| `R_SI_CMP_JUMP_FALSE` | 비교 + 분기 융합 |
| `R_SI_CMPI_JUMP_FALSE` | 즉시값 비교 + 분기 융합 |
| `R_ADDI_JUMP` | 증가 + 점프 융합 |
| `R_SI_ADDI_CMPI_LT_JUMP` | 증가 + 상한 비교 + 분기 융합 |
| `R_SI_MODI_ADD_STORE` | `dst = acc + (src % imm)` 융합 |
| `R_SI_LOOP_STEP` | 루프 전체 스텝 융합 |

#### 인라인 캐시 (IC)

`CompactInstruction`의 `mutable ic_shape` / `mutable ic_slot` 필드를 캐시 저장소로 활용합니다.

- **R_BUILD_STRUCT IC**: 첫 번째 실행 후 `StructTypeObject*`를 캐시. 이후 호출은 타입 탐색·문자열 비교 없이 객체를 직접 할당합니다.
- **StructTypeObject::cached_shape**: `initialize_struct_instance()`의 Shape 계산(벡터 할당 + 해시맵 조회)을 첫 번째 호출 시 1회로 제한합니다.

### 3.3 레지스터 스필 오버플로우 (Register Spill Fallback)
단일 함수가 256개 이상의 로컬 변수를 사용할 경우, 컴파일러는 초과분을 가리키는 스필 명령어(Spill opcodes)를 발행합니다. 스필 데이터 메모리는 콜 프레임에 부착되는 일시적인 `GcArray`로 작동합니다.

### 3.4 코루틴 모델 (Coroutine Model)
각 코루틴은 독립적으로 힙(Heap)에 할당되는 `CoroutineFrame`이며, 다음을 저장합니다:
- 현재까지의 모든 레지스터 상태 값 스냅샷
- 피연산자 스택(Operand stack)
- 중지된 PC 위치

`yield` 실행 시 현재 진행 상황을 힙에 직렬화하며, 불필요한 빈 여유 버퍼는 최소 크기로 동적 축소(Compact)시킵니다.

### 3.5 모듈 바이트코드 캐싱 (Module Bytecode Caching)
컴파일된 `.zph` 모듈은 원본 소스와 같은 경로에 `.zphc` 포맷으로 캐시됩니다. 마지막 수정 시간(mtime)을 비교하여 원본 코드가 수정되지 않은 이상 파싱/컴파일 과정을 생략하고 즉시 캐시본을 활용합니다.

## 4. 값 모델과 타입 계층 (Value System and Type Hierarchy)

### 4.1 값의 표현 방식 (NaN-boxing)
ZephyrVM의 모든 값은 단일 64비트 공간을 활용하는 NaN-boxing 구조에 의해 표현됩니다. 이를 통해 실수(Float), 포인터, 그리고 작은 크기의 정수나 Bool 값을 메모리 패딩(Padding) 낭비 없이 동일한 8바이트 안에 안전하게 보관합니다.

### 4.2 내장 타입 체계 (Built-in Types)

| 타입 (Type) | GcObjectType | 설명 |
|---|---|---|
| `string` | `GC_STRING` | Interned 기반, 불변 |
| `Array` | `GC_ARRAY` | 동적 무타입 컨테이너 |
| `StructInstance` | `GC_STRUCT` | 구조체 필드 묶음 |
| `EnumInstance` | `GC_ENUM` | Variant 구조 및 Payload 내포 보관 |
| `Closure` | `GC_CLOSURE` | 함수 포인터 + 캡처 배열(Upvalue) |
| `CoroutineFrame` | `GC_COROUTINE` | 힙 메모리에 상주하는 호출 스택 |

### 4.3 호스트 핸들 시스템 (Host Handle System)
게임 엔진과 같이 호스트(C++)에서 주입된 객체 참조는 복잡하고 무거운 역방향 GC 대상 래퍼(Wrapper) 씌우기를 배제하고, 순수한 ID + 세대 코드 숫자로만 보호되는 안전한 핸들 토큰(Handle Token) 상태로 스크립트에 들어옵니다. (Frame, Tick, Persistent, Stable 4단계 수명 주기 지원)

## 5. 가비지 컬렉터 (Garbage Collector)

### 5.1 세대적 설계 (Generational Design)
새로 생성된 객체는 무조건 **Nursery(젊은 세대)**에 할당됩니다. 이후 마이너(Minor) GC 때 살아남을 경우 구세대(Old generation)로 즉시 승급(Promotion)시킵니다.

- **Minor GC** — Nursery 범위 객체만 집중 탐색 및 수거 (Remembered Set 활용)
- **Major GC** — 전체 세대를 탐색하는 전통적인 Tri-color Mark-Sweep

### 5.2 쓰기 배리어와 카드 테이블 (Write Barrier and Card Table)
오래된 부모 객체가 방금 생성된 새로운 자식 객체를 가리키도록 소유권을 수정했을 때만 배리어(Barrier)가 작동하여 비트맵 방식의 카드 테이블에 해당 구역의 오염 플래그를 등록합니다. 이를 통해 Minor GC는 모든 부모를 확인하지 않아도 손쉽게 젊은 객체의 생명선(Root)을 찾을 수 있습니다.

### 5.3 증분 수거 (Incremental Collection)
가비지 컬렉터 스텝 함수 `gc_step`은 한 프레임에 할당된 1ms 등의 Time Budget 만큼만 Mark나 Sweep 루프 단계를 전진시키고 스스로 중단합니다. 이는 실시간 게임의 심각한 프레임 드랍(Spike)을 미연에 방지합니다.

## 6. 타입 시스템 (Type System)

### 6.1 트레이트 (Traits and impl)
컴파일러 의미 분석기는 트레이트에 명시된 규격 메서드가 모두 `impl` 내부에서 적합하게 오버라이드 구현되었는지 100% 검증합니다.

### 6.2 제네릭 (Generics)
컴파일 타임 중에 단형화(Monomorphisation) 패스가 제네릭 함수에 투입된 타입을 기준으로 각기 독립된 바이트코드 복제본을 만듭니다.

### 6.3 Result\<T\> 와 에러 전파 (Error Propagation)
함수 바디 등에서 실행되는 `?` 기호 표기법은 런타임 결과 값이 `Err`일 경우 즉시 부모 함수를 리턴 시키는 형태로 변환(Desugar)되어 작동합니다.

## 7. 모듈 임포트 트리 (Module System)

모듈의 탐색 우선 순위 체계:
1. 호출을 시작한 스크립트가 존재하는 현재 디렉토리 우선
2. 프로젝트 패키지 루트 (`set_package_root()` C++ API로 선언된 경로)
3. 호스트 엔진에서 등록한 가상 모듈
4. `std/` 내장 표준 라이브러리

## 8. 테스트 및 성능 베이스라인 (Test Infrastructure)

최종 벤치마크 평가 시스템은 새로운 소스코드 패치가 투입될 때마다, 기존 목표인 **`lua_baseline` (Lua 5.4 실행 속도 동등본)** 에 비해서 성능이 저하되었는지 확인하는 5단계 Acceptance Gate를 가집니다.

최신 결과 (2026-04-01):

| 테스트 항목 | 평균 지연 시간 | Lua 5.5 | 비율 | Gate |
|---|---|---|---|---|
| module_import | 838 µs | — | — | ✅ |
| hot_arithmetic_loop | ~420 µs | 394 µs | 1.07× | ✅ |
| array_object_churn | ~1,050 µs | 1,909 µs | **0.55×** | ✅ |
| host_handle_entity | ~224 µs | 303 µs | **0.74×** | ✅ |
| coroutine_yield_resume | ~220 µs | 923 µs | **0.24×** | ✅ |
