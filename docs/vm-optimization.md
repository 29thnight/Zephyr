# VM 최적화 — 슈퍼인스트럭션 & 인라인 캐시

Zephyr VM은 레지스터 기반 바이트코드 인터프리터로, 두 가지 핵심 최적화 기법을 통해 Lua 5.5 수준 또는 그 이하의 실행 비용을 달성합니다: **슈퍼인스트럭션 융합(Superinstruction Fusion)**과 **인라인 캐시(Inline Cache, IC)**.

---

## 1. 슈퍼인스트럭션 융합

### 개념

슈퍼인스트럭션이란 자주 연속해서 등장하는 2~3개의 바이트코드 패턴을 단일 opcode로 대체하는 컴파일 타임 최적화입니다. 디스패치 루프의 switch/case 진입 횟수를 줄여 명령어 하나당 고정 오버헤드(PC 증가, switch 분기, 캐시 미스 등)를 제거합니다.

융합은 `optimize_register_bytecode()` 내의 **피핵홀(peephole) 패스**에서 수행됩니다. 이 패스는 변경이 없을 때까지 반복(`while(changed)`)하며 인접 혹은 비인접 패턴을 탐색합니다.

### 현재 구현된 슈퍼인스트럭션

#### 기본 융합 (인접 패턴)

| 원본 패턴 | 융합 결과 | 의미 |
|---|---|---|
| `R_ADD/SUB/MUL` + `R_MOVE(dst, result)` | `R_SI_ADD/SUB/MUL_STORE` | 연산 + 목적지 이동을 한 번에 |
| `R_MODI(tmp, src, imm)` + `R_MOVE(dst, tmp)` | `R_MODI(dst, src, imm)` | 임시 레지스터 제거 |
| `R_ADDI(dst, src, imm)` + `R_JUMP(target)` | `R_ADDI_JUMP` | 증가 + 점프를 한 번에 |
| `R_CMP*` + `R_JUMP_IF_FALSE` | `R_SI_CMP_JUMP_FALSE` | 비교 + 조건부 점프 |
| `R_LOAD_INT(tmp, imm)` + `R_SI_CMP_JUMP_FALSE` | `R_SI_CMPI_JUMP_FALSE` | 즉시값 비교 + 조건부 점프 |
| `R_MODI(tmp, src, imm)` + `R_SI_ADD_STORE(dst, accum, tmp)` | `R_SI_MODI_ADD_STORE` | `dst = accum + (src % imm)` |

#### 비인접 루프 패턴

루프는 구조상 back-edge(반복 점프)와 loop-top(조건 검사)이 인접하지 않습니다. Zephyr는 `R_ADDI_JUMP`의 `ic_slot`(점프 대상 인덱스)을 기준으로 비인접 패턴을 탐지합니다.

| 패턴 | 융합 결과 | 의미 |
|---|---|---|
| `R_ADDI_JUMP(reg, +N)` → target: `R_SI_CMPI_JUMP_FALSE(reg, limit)` | `R_SI_ADDI_CMPI_LT_JUMP` | `reg += N; if reg < limit goto body` |
| `R_SI_MODI_ADD_STORE(accum, accum, iter, div)` + `R_SI_ADDI_CMPI_LT_JUMP(iter, step, limit)` | `R_SI_LOOP_STEP` | 루프 한 스텝 전체를 1 opcode로 |

### R_SI_LOOP_STEP 상세

가장 고도로 융합된 슈퍼인스트럭션입니다. 다음 세 연산을 단일 opcode로 처리합니다:

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

**인코딩 (24바이트 CompactInstruction 내):**

| 필드 | 역할 |
|---|---|
| `dst` (uint8) | 누적 레지스터 (accum) |
| `src1` (uint8, int8 재해석) | 반복 증가 폭 (step) |
| `src2` (uint8) | 루프 카운터 레지스터 (iter) |
| `operand_a` (uint8) | 나머지 제수 (div, 1~255) |
| `ic_slot[15:0]` (uint16) | 루프 본문 시작 주소 (body_start) |
| `ic_slot[31:16]` (int16) | 루프 종료 조건 상한 (limit) |

**융합 조건:**
1. `R_SI_MODI_ADD_STORE`의 `dst == src1` (자기 누적: `accum = accum + ...`)
2. `R_SI_ADDI_CMPI_LT_JUMP`의 `reg == src2` (동일 루프 카운터)
3. `body_start ≤ 0xFFFF` (uint16 범위)
4. `step` ∈ [-128, 127], `limit` ∈ [-32768, 32767]

**효과 (hot_arithmetic 기준):**

| 단계 | ops/iteration | 평균 시간 |
|---|---|---|
| 기본 레지스터 VM | ~6 | 2,170 µs |
| +R_SI_MODI_ADD_STORE | 3 | 692 µs |
| +R_SI_ADDI_CMPI_LT_JUMP | 2 | 516 µs |
| **+R_SI_LOOP_STEP** | **1** | **~420 µs** |

---

## 2. 인라인 캐시 (Inline Cache)

### 개념

인라인 캐시는 실행 중 처음 한 번 계산한 타입/형상 정보를 명령어 자체의 필드(`ic_shape`, `ic_slot`)에 저장하고, 이후 같은 타입이 들어오면 비싼 탐색을 건너뛰는 기법입니다.

`CompactInstruction`의 뮤터블 필드를 활용합니다:
```cpp
mutable Shape*   ic_shape;   // 캐시된 Shape* 또는 타입 포인터
mutable uint32_t ic_slot;    // 캐시된 필드 인덱스 또는 상태 플래그
```

### R_BUILD_STRUCT IC

구조체 리터럴을 생성하는 `R_BUILD_STRUCT` opcode는 실행마다 다음 작업을 수행합니다:

**Cold path (IC 미적용 시 매 호출):**
1. `parse_type_name()` — 타입 이름 문자열을 `::` 기준으로 파싱
2. `expect_struct_type()` — 환경 체인 순회 + `unordered_map` 조회
3. 임시 `std::vector<Value> bs_fields(count)` 힙 할당
4. `allocate<StructInstanceObject>()` — 객체 할당
5. `initialize_struct_instance()` — shape 조회 + `field_values.assign(N, nil)`
6. 각 필드마다 `field_slot(name)` (이름 → 인덱스 문자열 조회) + `enforce_type` + `validate_handle_store`

**Warm path (IC 적용 후):**
1. `ic_shape != nullptr && ic_slot == 1` 체크 (분기 하나)
2. `reinterpret_cast<StructTypeObject*>(ic_shape)` 로 타입 포인터 획득
3. `allocate<StructInstanceObject>(type)` — 객체 할당
4. `bs_inst->shape = type->cached_shape` (캐시된 Shape* 직접 설정)
5. `field_values.reserve(N)` + `push_back` × N — 단일 패스 필드 초기화
6. `note_struct_field_write()` — 쓰기 장벽 (young 객체면 사실상 no-op)

파싱, 환경 순회, 문자열 조회, 타입 검사가 전부 제거됩니다.

**IC 설정 조건 (첫 번째 호출 후):**
- 결과 객체가 `StructInstance`임을 확인
- `metadata.names[i] == type->fields[i].name` — 선언 순서와 필드 순서가 일치
- 조건 충족 시: `ic_shape = type`, `ic_slot = 1`

### StructTypeObject Shape 캐시

`initialize_struct_instance()`는 매번 `shape_for_struct_type()`을 호출했습니다:

```cpp
// 기존: 매 호출마다
std::vector<std::string> field_names = collect_struct_field_names(type); // 벡터 할당
std::string key = make_key(field_names);                                 // 문자열 합성
Shape* shape = Shape::cache_.find(key)->second;                         // 해시맵 조회
```

`StructTypeObject`에 `mutable Shape* cached_shape = nullptr` 필드를 추가하여, 첫 번째 인스턴스 생성 시 한 번만 계산하고 이후 직접 반환합니다:

```cpp
// 개선: 첫 번째만 계산
if (type->cached_shape == nullptr)
    type->cached_shape = shape_for_struct_type(type);
instance->shape = type->cached_shape;
```

---

## 3. 최종 벤치마크 (vs Lua 5.5)

| 케이스 | 최적화 전 (v1) | 현재 | Lua 5.5 | 비율 |
|---|---|---|---|---|
| hot_arithmetic | 1,000 ms | ~420 µs | 394 µs | **1.07×** |
| array_object_churn | — | ~1,050 µs | 1,909 µs | **0.55×** ✓ |
| host_handle_entity | — | ~224 µs | 303 µs | **0.74×** ✓ |
| coroutine_yield_resume | — | ~220 µs | 923 µs | **0.24×** ✓ |

array_churn은 R_BUILD_STRUCT IC + Shape 캐시 적용 후 **56% 개선** (2,330 µs → 1,050 µs), Lua 대비 **약 2배 빠릅니다.**

---

## 4. 슈퍼인스트럭션 디버깅

`zephyr dump-bytecode <file>` 명령으로 융합된 opcode를 확인할 수 있습니다:

```
0  R_LOAD_INT      dst=r0 value=0          ; sum = 0
1  R_LOAD_INT      dst=r1 value=0          ; i = 0
2  R_SI_LOOP_STEP  accum=r0 iter=r1 div=3 step=1 limit=70000 body=2
3  R_RETURN        src=r0
```

`zephyr stats <file>` 명령은 슈퍼인스트럭션 융합 횟수와 히트율을 출력합니다:

```
superinstruction_fusions: 3 (hit_rate: 75.00%)
```
