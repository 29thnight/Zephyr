# Project Zephyr GC — Fragmentation-Resistant Refactor: Implementation Analysis

> **목적**: Moving GC 없이 단편화를 줄이는 멀티-스페이스 힙으로 현재 GC를 리팩터링하기 위한
> 구현 지향 코드 분석 문서. 모든 심볼·라인 번호는 `src/zephyr.cpp` 및 `include/zephyr/api.hpp` 실측 기준.

---

## 목차

1. [현재 GC 구현 맵](#1-현재-gc-구현-맵)
2. [할당 경로 분해](#2-할당-경로-분해)
3. [힙 토폴로지 가정 분석](#3-힙-토폴로지-가정-분석)
4. [헤더·메타데이터 재설계 영향](#4-헤더메타데이터-재설계-영향)
5. [Sweep·Reclaim 재설계](#5-sweepreclaim-재설계)
6. [Barrier·Remembered Set 호환성 분석](#6-barrierremembered-set-호환성-분석)
7. [루트·객체 추적 호환성](#7-루트객체-추적-호환성)
8. [기존 코드 기반 자료구조 제안](#8-기존-코드-기반-자료구조-제안)
9. [단계별 마이그레이션 계획](#9-단계별-마이그레이션-계획)
10. [리스크 레지스터](#10-리스크-레지스터)
11. [계측 계획](#11-계측-계획)
12. [구체적 TODO 체크리스트](#12-구체적-todo-체크리스트)

---

## 1. 현재 GC 구현 맵

### 1.1 객체 할당

| 심볼 | 위치 | 역할 | 영향받는 Space |
|------|------|------|---------------|
| `Runtime::allocate<T>()` | zephyr.cpp:2281–2309 | 유일한 GcObject 생성 진입점. `new T()` 후 `objects_` 헤드 삽입, `should_allocate_old` 판정 | 모든 Space 분리 |
| `Runtime::should_allocate_old()` | zephyr.cpp:3644–3646 | `Coroutine \| Environment \| size >= 4KB` → old 직접 배치 | NurserySpace, OldSmallSpace, LOS |
| `Runtime::is_old_object()` | zephyr.cpp:3648–3650 | `GcOldBit` 플래그 확인 | Space 기반 판정으로 대체 또는 보완 |
| `Runtime::is_large_object()` | zephyr.cpp:3652–3654 | `size_bytes >= large_object_threshold_bytes_` | LargeObjectSpace 라우팅 |
| `objects_` | zephyr.cpp:2558 (decl), 2294–2295 (write) | **전체 힙 단일 연결 리스트 헤드** | Space 분리 후 제거 대상 |
| `live_bytes_` | zephyr.cpp:2586 | 전역 힙 계산 | per-space 계정으로 분산 |
| `allocation_pressure_bytes_` | zephyr.cpp:2587 | full GC 트리거 임계값 | per-space pressure로 분산 |
| `young_allocation_pressure_bytes_` | zephyr.cpp:2588 | young GC 트리거 | `NurserySpace::used_bytes` |

### 1.2 객체 헤더 레이아웃

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `GcHeader` | zephyr.cpp:1698–1706 | 모든 GcObject 내장 메타데이터 | `space_kind`, `size_class`, `movable` 비트 추가 필요 |
| `GcColor` | zephyr.cpp:1665–1669 | 삼색 마킹 상태 (White/Gray/Black) | 그대로 유지 |
| `GcFlags` (enum) | zephyr.cpp:1683–1690 | `Dirty`, `MinorRemembered`, `Finalizable`, `Pinned`, `Old`, `DebugPoisoned` | `GcPinnedBit` 현재 logic 미사용(dead). `SpaceKind` 비트 추가 필요 |
| `GcObject` | zephyr.cpp:1708–1724 | 모든 GC 객체 베이스. `trace()` virtual | `operator new/delete` 오버라이드 추가 대상 |

> ⚠️ **주의**: `GcPinnedBit`은 line 1687에 정의되어 있으나 sweep, barrier, promote 어느 경로에서도 분기에 사용되지 않음. 사실상 dead flag.

### 1.3 Young / Old 할당

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `nursery_trigger_bytes_` | zephyr.cpp:~2618 | 32KB 기본값. young GC 트리거 | `NurserySpace::commit_limit` |
| `large_object_threshold_bytes_` | zephyr.cpp:~2619, 3541 | 4KB. old 직접 배치 판정 | LOS threshold |
| `gc_young_cycle_requested_` | zephyr.cpp:2584 | young GC 요청 플래그 | NurserySpace pressure signal |

### 1.4 Promotion

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `Runtime::promote_object()` | zephyr.cpp:3656–3677 | `GcOldBit` 설정, 카드 재구성, `remember_minor_owner` 호출 | OldSmallSpace 또는 PinnedSpace로 라우팅 |
| `rebuild_environment_cards()` | zephyr.cpp:3989–4000 | promote 시 env 카드 재구성 | 카드 소유권이 space로 이동 시 수정 |
| `rebuild_array_cards()` | zephyr.cpp:~4002 | 배열 카드 재구성 | 동일 |
| `rebuild_struct_cards()` | zephyr.cpp:~4012 | struct 카드 재구성 | 동일 |
| `rebuild_enum_cards()` | zephyr.cpp:~4022 | enum 카드 재구성 | 동일 |
| `rebuild_coroutine_cards()` | zephyr.cpp:~4032 | coroutine 카드 재구성 | 동일 |
| `promotion_survival_threshold_` | zephyr.cpp:~2620 | 기본값 2. age 도달 시 promote | 유지 |

### 1.5 Sweep

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `Runtime::sweep()` | zephyr.cpp:8201–8234 | full GC sweep. `next_all` 체인 전수 순회, White → delete | Space별 reclaim으로 분리 |
| `Runtime::sweep_young_objects()` | zephyr.cpp:8743–8786 | young GC sweep. 전체 `objects_` 체인 순회 | `NurserySpace::sweep()` 으로 이전 |
| `sweep_cursor_` | zephyr.cpp:2582 | full sweep 진행 커서 | per-space sweep cursor |
| `sweep_previous_` | zephyr.cpp:~2583 | dead object 언링크용 이전 포인터 | per-space 관리 |
| `Runtime::process_detach_queue()` | zephyr.cpp:8236–8247 | finalizable 객체 처리 | space-aware free로 변경 |
| `detach_queue_` | zephyr.cpp:2567 | finalizable 객체 큐 | cross-space, 유지 |

### 1.6 Full Mark Cycle

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `Runtime::begin_gc_cycle()` | zephyr.cpp:8137–8153 | 전체 `objects_` 순회하여 모든 색 White로 초기화 | Space 분리 후 per-space 초기화 |
| `Runtime::seed_roots()` | zephyr.cpp:8155–8158 | `mark_roots()` 호출 후 phase 전환 | 유지 |
| `Runtime::drain_gray()` | zephyr.cpp:8184–8199 | `gray_stack_` 소비, `object->trace()` 호출 | 유지 (space-agnostic) |
| `Runtime::process_dirty_roots()` | zephyr.cpp:8160–8182 | dirty 큐 재마킹, `sweep_cursor_` 초기화 | sweep_cursor_가 per-space가 되면 수정 |
| `Runtime::mark_object()` | zephyr.cpp:3563–3569 | White → Gray, `gray_stack_` push | 유지 |
| `Runtime::mark_value()` | zephyr.cpp:3556–3560 | Value에서 GcObject 추출 후 mark_object | 유지 |
| `gray_stack_` | zephyr.cpp:2559 | cross-space 공유 gray 스택 | 유지 (space-agnostic) |

### 1.7 Write Barrier / Card 마킹

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `note_write(Environment*, const Value&)` | zephyr.cpp:8258–8274 | env 대상 incremental + inter-gen 장벽 | space dispatch 추가 |
| `note_write(GcObject*, const Value&)` | zephyr.cpp:8276–8290 | GcObject 대상 장벽 | 동일 |
| `note_environment_binding_write()` | zephyr.cpp:4048–4059 | env 카드 + note_write | LOS/Pinned 분기 추가 |
| `note_array_element_write()` | zephyr.cpp:4061–4071 | 배열 카드 + note_write | 동일 |
| `note_struct_field_write()` | zephyr.cpp:4073–4083 | struct 카드 + note_write | 동일 |
| `note_enum_payload_write()` | zephyr.cpp:4085–4095 | enum 카드 + note_write | 동일 |
| `kGcValueCardSpan` | zephyr.cpp:1692 | 카드당 16 Value | 유지 또는 bitmap으로 교체 |
| `barrier_hits_` | zephyr.cpp:2598 | 장벽 호출 횟수 카운터 | 유지 |

### 1.8 Remembered Set

| 심볼 | 위치 | 역할 | 영향 |
|------|------|------|------|
| `remembered_objects_` | zephyr.cpp:2566 | old→young 참조 보유 객체 목록 | cross-space remembered set으로 유지 |
| `Runtime::remember_minor_owner()` | zephyr.cpp:3972–3987 | `GcMinorRememberedBit` 설정, remembered_objects_ push | 유지 |
| `Runtime::compact_minor_remembered_set()` | zephyr.cpp:4135–4159 | young GC 후 stale 항목 제거 | 유지 |
| `Runtime::rebuild_minor_remembered_set()` | zephyr.cpp:4161–4194 | full GC Complete 단계에서 전체 objects_ 재구성 | ⚠️ objects_ 2회 전순회. per-space 분산 필요 |
| `Runtime::count_remembered_cards()` | zephyr.cpp:4097–4133 | 통계용 전체 카드 집계. objects_ 전순회 | per-space 집계로 변경 |

### 1.9 Root 열거

| 심볼 | 위치 | 역할 |
|------|------|------|
| `Runtime::visit_root_references()` | zephyr.cpp:3601–3642 | 9개 루트 카테고리 열거. space-agnostic이므로 유지 |
| `root_environment_` | zephyr.cpp:2560 | 글로벌 env 루트 → PinnedSpace |
| `active_environments_` | zephyr.cpp:2561 | 활성 스택 env |
| `rooted_value_vectors_` / `rooted_values_` | zephyr.cpp:2562–2563 | C++ 스택 rooted 값 |
| `active/suspended/retained_coroutines_` | zephyr.cpp:2568–2570 | 코루틴 루트 |
| `interned_strings_` | zephyr.cpp:2573 | 인턴 문자열 맵 → PinnedSpace 또는 별도 string pool |
| `native_callback_registry_` | zephyr.cpp:2574 | 네이티브 콜백 → PinnedSpace |
| `pinned_debug_objects_` | zephyr.cpp:2576 | 디버그 핀 객체 → PinnedSpace |
| `modules_` | zephyr.cpp:2571 | 모듈 env + namespace → PinnedSpace |

### 1.10 Finalization / Detach Queue

| 심볼 | 위치 | 역할 |
|------|------|------|
| `GcFinalizableBit` | zephyr.cpp:1686 | finalizable 마커. Space 전반에 유지 |
| `detach_queue_` | zephyr.cpp:2567 | finalizable 수거 대기 큐. cross-space 유지 |
| `process_detach_queue()` | zephyr.cpp:8236–8247 | 큐 소비 후 `delete`. → space-aware free로 교체 |

> `sweep()` (line 8201)과 `sweep_young_objects()` (line 8743) 모두 `GcFinalizableBit` 확인 후 `detach_queue_` push. 두 경로가 독립적으로 유지되어 있음.

### 1.11 Debug Verification / Stress Mode

| 심볼 | 위치 | 역할 |
|------|------|------|
| `Runtime::gc_verify_full()` | zephyr.cpp:4306+ | 전체 힙 정합성. objects_ 전순회 → per-space 확장 필요 |
| `Runtime::gc_verify_young()` | zephyr.cpp:8849–8933 | young 수집 후 검증. objects_ 전순회 포함 → NurserySpace 검증으로 변환 |
| `maybe_run_gc_stress_safe_point()` | zephyr.cpp:8249–8256 | 스트레스 safe-point (line 5139, 6174에서 호출). 유지 |
| `DEBUG_LEAK_CHECK` 매크로 | zephyr.cpp:1710–1712 | `on_gc_object_created/destroyed` 카운터 (line 159–168). 유지 |

---

## 2. 할당 경로 분해

### 경로 1: 일반 Young 객체 (`ArrayObject`, `StructInstanceObject`, `StringObject` 등)

```
Entry: Runtime::allocate<T>(args...)
  line 2282: new T(args...)               ← 시스템 malloc, 커스텀 알로케이터 없음
  line 2283: header.size_bytes = sizeof(T) ← nested heap 미포함
  line 2284: header.color = gc_marking() ? Black : White
  line 2285: flags &= ~(DirtyQueued | MinorRemembered)
  line 2288: should_allocate_old(kind, size) → false → GcOldBit NOT set
  line 2294: header.next_all = objects_    ← 헤드에 삽입 (LIFO)
  line 2295: objects_ = object
  line 2296: live_bytes_ += sizeof(T)
  line 2297: allocation_pressure_bytes_ += sizeof(T)
  line 2299: young_allocation_pressure_bytes_ += sizeof(T)
  line 2302: if young_pressure >= nursery_trigger → gc_young_cycle_requested_ = true
  line 2305: if total_pressure >= incremental_trigger → gc_cycle_requested_ = true
```

- **Fast-path**: 없음. 모든 young 할당이 동일 경로 통과.
- **GC 트리거**: allocate 내부 (lines 2302–2306). 플래그만 설정, 즉시 실행 안 함.
- **next_all 의존**: NurseryChunk 도입 시 헤드 삽입을 nursery 내부 관리로 이전해야 함.

---

### 경로 2: Old 직접 할당 (`Environment`, `CoroutineObject`)

```
Entry: Runtime::allocate<T>(args...)
  line 2282: new T(args...)
  line 2288: should_allocate_old(kind, size) → true (Coroutine || Environment)
  line 2289: flags |= GcOldBit              ← old로 즉시 배치
  line 2294: header.next_all = objects_     ← 동일 전역 리스트 삽입
  line 2296: live_bytes_ += sizeof(T)
  line 2297: allocation_pressure_bytes_ += sizeof(T)
             young_allocation_pressure_bytes_ NOT incremented
```

> ⚠️ old 직접 할당 객체도 `objects_` 동일 리스트에 삽입됨. young/old 분리는 `GcOldBit` 플래그로만 구분.

---

### 경로 3: Large Object 할당 (size >= 4KB)

```
Entry: Runtime::allocate<T>(args...)
  line 2288: should_allocate_old(kind, size >= 4096) → true
  line 2289: flags |= GcOldBit
  line 2294: header.next_all = objects_     ← LOS 분리 없음. small과 동일 리스트 혼재
```

> `is_large_object()` (line 3652)는 통계·promotion용으로만 사용. **현재 LOS 분리 없음**.

---

### 경로 4: Pinned 객체 할당

```
현재 상태:
  GcPinnedBit이 정의되어 있으나 allocate<T>에서 설정 코드 없음.
  pinned_debug_objects_ (line 2576)는 외부에서 수동 push.
  host_handles_ (line 2575)의 HostHandleEntry.instance = weak_ptr<void>
    → GC 핀이 아닌 C++ 외부 수명 추적만 수행.

실제 "핀" 동작:
  pinned_debug_objects_에 등록된 객체가 visit_root_references()에서 루트로 열거됨.
  → 핀이 아닌 영구 루트 방식.
```

---

### 경로 5: CoroutineObject 할당

```
Entry: Runtime::allocate<CoroutineObject>(...)
  should_allocate_old(Coroutine, sizeof(CoroutineObject)) → true
  GcOldBit 설정, objects_ 삽입
  내부 frames 벡터는 동적 push_back으로 확장
  각 CoroutineFrameState 내 ~15개 std::vector가 별도 malloc으로 생성
```

> **CoroutineArena 기회**: CoroutineObject와 프레임들을 단일 arena 블록으로 배치하면 suspend/resume 캐시 지역성 극적 개선.

---

### 경로 6: Environment 할당

```
Entry: Runtime::allocate<Environment>(parent, kind)
  line 3544: root_environment_ 할당 시 즉시 promote_object(root_environment_) 호출
  should_allocate_old(Environment, sizeof(Environment)) → true
  GcOldBit 즉시 설정
  내부 unordered_map, vector들은 별도 malloc
```

> **EnvironmentArena 기회**: Local Environment들은 함수 호출과 생명주기 일치. 스코프 arena에서 배치 가능.

---

## 3. 힙 토폴로지 가정 분석

### Assumption A: 단일 `objects_` 리스트가 전체 힙

| 위치 | 코드 | Space 분리 시 문제 |
|------|------|--------------------|
| `allocate<T>()` line 2294–2295 | `header.next_all = objects_; objects_ = object;` | 어느 space 리스트에 삽입할지 결정 불가 |
| `Runtime::~Runtime()` line 3549–3553 | `while (objects_) { delete ...; }` | Space별 소멸자로 분산 필요 |
| `begin_gc_cycle()` line 8144–8148 | `for (object = objects_; ...)` color 전체 초기화 | per-space 초기화 루프로 분산 |
| `rebuild_minor_remembered_set()` line 4162–4193 | `objects_` 2회 전순회 | **가장 비싼 전순회**. per-space iteration으로 분산 |
| `count_remembered_cards()` line 4099–4133 | `objects_` 전순회 | per-space 집계로 변경 |

### Assumption B: Sweep이 단일 체인 순회에 의존

| 위치 | 코드 | 문제 |
|------|------|------|
| `sweep()` line 8201–8228 | `sweep_cursor_` + `sweep_previous_` 로 `next_all` 패치 | Space별 sweep iterator 필요 |
| `sweep_young_objects()` line 8744–8778 | `objects_` 직접 순회 | `NurserySpace::sweep()` 으로 이전 |

### Assumption C: 단일 전역 계정 카운터

| 위치 | 문제 |
|------|------|
| `live_bytes_` (8220, 8240, 8763, 8783에서 감산) | 어느 Space에서 해제되었는지 구분 없음 |
| `allocation_pressure_bytes_` | per-space pressure 신호로 분산 필요 |

### Assumption D: `GcOldBit` 플래그가 유일한 세대 판별

Space 분리 후 `space_kind` enum으로 대체. 플래그와 space_kind 불일치 가능성이 생김.

### 대체 추상화

```
Assumption A/B → HeapSpace::for_each_object(callback)
  모든 space가 구현. begin_gc_cycle, rebuild_minor_remembered_set,
  count_remembered_cards, verify가 이 API를 통해 전체 힙 순회.

Assumption C → HeapSpace::live_bytes, used_bytes, reserved_bytes
  Runtime::live_bytes_ = sum(all spaces).

Assumption D → GcHeader::space_kind (enum)
  is_old_object() → object->header.space_kind != SpaceKind::Nursery

Assumption E → HeapSpace::verify(VerifyContext&) 가상 메서드
  gc_verify_full이 각 space의 verify를 순차 호출.
```

---

## 4. 헤더·메타데이터 재설계 영향

### 현재 GcHeader (~28 bytes)

```cpp
// zephyr.cpp:1698-1706
struct GcHeader {
    uint32_t  size_bytes;    // 4B
    uint16_t  type_id;       // 2B
    GcColor   color;         // 1B
    uint8_t   flags;         // 1B  (6비트 사용, 2비트 여유)
    uint8_t   age;           // 1B
    // 3B padding
    GcObject* next_all;      // 8B
    GcObject* next_gray;     // 8B
};
// Total: ~28B
```

### 필드별 이전 권고

| 필드 | 권고 | 이유 |
|------|------|------|
| `size_bytes` | **GcHeader 유지** | sweep, promote, LOS 경계 판정에 필요 |
| `type_id` | **GcHeader 유지** | trace() dispatch vtable이 필요 |
| `color` | **GcHeader 유지** | gray_stack_이 cross-space이므로 헤더에 보유 |
| `flags` | **GcHeader 유지 + 비트 재정의** | 6비트 사용 중, `GcMovableBit` 추가 가능 |
| `age` | **GcHeader 유지** | NurserySpace에서만 의미있지만 접근 비용 최소화 |
| `next_all` | **장기 제거, 단기 per-space 체인으로 재사용** | Stage 6에서 제거 시 8B 절감 |
| `next_gray` | **GcHeader 유지** | gray_stack_이 cross-space이므로 intrusive 링크 헤더에 보유 |

### 확장 권고안 (Option B: 독립 필드 추가)

```cpp
enum class GcSpaceKind : uint8_t {
    Nursery     = 0,
    OldSmall    = 1,
    LargeObject = 2,
    Pinned      = 3,
    EnvArena    = 4,  // 선택적
    CoroArena   = 5,  // 선택적
};

struct GcHeader {
    uint32_t    size_bytes;
    uint16_t    type_id;
    GcColor     color;
    uint8_t     flags;         // GcMovableBit (1<<6) 추가
    uint8_t     age;
    GcSpaceKind space_kind;    // +1B (신규)
    uint8_t     size_class;    // +1B (OldSmallSpace용, 신규)
    // padding 재정렬 → 2B 순증가
    GcObject*   next_all;      // 임시 유지 (Stage 6에서 제거)
    GcObject*   next_gray;     // 유지
};
// Stage 6 이후 next_all 제거 시: ~20B (-8B)
```

---

## 5. Sweep·Reclaim 재설계

### 현재 `Runtime::sweep()` 상세 (lines 8201–8234)

```
1. sweep_cursor_ = objects_  (process_dirty_roots 종료 시 line 8179에서 설정)
2. while (budget_work > 0 && sweep_cursor_):
   - White → GcFinalizableBit 있으면 detach_queue_.push_back + unlink
   - White → 없으면 unlink + live_bytes_ -= size + delete current  ← line 8221
   - 그 외 → color = White (다음 사이클 리셋) + sweep_previous_ = current
   - sweep_cursor_ = next; --budget_work
3. sweep_cursor_ == nullptr → phase = DetachQueue or Complete
```

**핵심 특성**:
- `delete current` (line 8221) — `::operator delete` 직접 호출. 커스텀 free 경로 없음.
- 언링크: `sweep_previous_->header.next_all = next` 패치 방식.
- Reclaim이 centralized: 타입별 경로 없음. 모든 타입이 동일 `delete`.
- `live_bytes_` 감산이 `sweep`과 `process_detach_queue` 양쪽에 분산.

### Space별 Reclaim 재설계

#### NurserySpace reclaim
```
현재: sweep_young_objects()가 objects_ 전체를 순회하며 young 필터링
개선:
  NurseryChunk는 bump-pointer 방식.
  NurserySpace::sweep_chunk(chunk):
    for each live (Gray/Black) young object in chunk:
        if age + 1 >= threshold: promote_to_old(obj)
        else: ++age; color = White
    chunk->bump_ptr = chunk->base   ← 청크 전체 재사용 (per-object free 없음)
```

#### OldSmallSpace span sweep
```
Span(페이지 단위) 내 객체들을 sweep 시 bitmap으로 처리:
  OldSmallSpace::sweep_span(span):
    for each slot i with live_bitmap[i] == 1:
        obj = slot_object(span, i)
        if obj->color == White:
            free_bitmap[i] = 1; live_bitmap[i] = 0
            live_bytes_ -= slot_size
            if GcFinalizableBit: detach_queue_.push(obj)
            else: obj->~GcObject()  ← 소멸자 호출, slab은 유지
            --span->live_slots; --budget
        else:
            obj->color = White
    if span->live_count == 0: → empty_spans
    else if sparse: → partial_spans
```

#### LargeObjectSpace reclaim
```
LOS::sweep():
  for each LargeObjectNode* node in los_list_:
    if node->object->color == White:
        unlink(node)
        live_bytes_ -= node->size
        coalesce_and_free(node)  ← 인접 free block 병합, 가능하면 OS decommit
    else:
        node->object->color = White
```

#### PinnedSpace reclaim
```
sweep 단계에서 PinnedSpace는 color 리셋만 수행, delete 없음.
명시적 unpin_object(GcObject*) 호출 시에만 해제.
PinnedSpace::sweep(): for each pinned obj: obj->color = White
```

### `process_detach_queue()` 변경

```cpp
// 현재: line 8237-8241
delete object;
live_bytes_ -= size_bytes;

// 변경:
object->get_space()->free_object(object);  // space-aware free (live_bytes_ 감산 내부 처리)
```

---

## 6. Barrier·Remembered Set 호환성 분석

### 모든 Barrier 진입점

| 함수 | 위치 | 트리거 조건 |
|------|------|------------|
| `note_environment_binding_write()` | 4048–4059 | env 바인딩 쓰기 |
| `note_array_element_write()` | 4061–4071 | 배열 원소 쓰기 |
| `note_struct_field_write()` | 4073–4083 | struct 필드 쓰기 |
| `note_enum_payload_write()` | 4085–4095 | enum payload 쓰기 |
| `note_write(Environment*, const Value&)` | 8258–8274 | 위 4개에서 호출 (env 전용) |
| `note_write(GcObject*, const Value&)` | 8276–8290 | 위 4개에서 호출 (일반) |

### 카드 마킹 저장 방식

```
각 GcObject 서브클래스가 자체 vector<uint8_t> remembered_cards 보유:
  ArrayObject::remembered_cards
  Environment::remembered_cards (Local kind만)
  StructInstanceObject::remembered_cards
  EnumInstanceObject::remembered_cards
  CoroutineFrameState::stack_cards, local_cards (per-frame)

카드 인덱스 = value_index / kGcValueCardSpan (16)
카드 값 = 0 (clean) or 1 (dirty)
카드 벡터는 쓰기 시 lazy resize (각 note_*_write에서 resize 호출)
```

### Space 분리 시 barrier 변경

```cpp
// 변경 후 note_write(GcObject* owner, const Value& value):
void Runtime::note_write(GcObject* owner, const Value& value) {
    ++barrier_hits_;
    GcObject* val_obj = value.as_object();
    if (owner && val_obj) {
        // is_old_object() → space_kind != Nursery 로 전환 (Stage 6)
        const bool owner_is_old = owner->header.space_kind != GcSpaceKind::Nursery;
        const bool val_is_young = val_obj->header.space_kind == GcSpaceKind::Nursery;
        if (owner_is_old && val_is_young) {
            remember_minor_owner(owner);
        }
    }
    if (gc_marking()) {
        // incremental barrier — space 무관, 동일
        if (owner && !(owner->header.flags & GcDirtyQueuedBit)) {
            owner->header.flags |= GcDirtyQueuedBit;
            dirty_objects_.push_back(owner);
        }
        mark_value(value);
    }
}
```

### LOS / PinnedSpace barrier 참여

- **LOS → Nursery**: inter-gen 장벽 동일 적용. `remembered_objects_` 등록.
- **Pinned → Nursery**: `remember_minor_owner()` line 3978의 Root/Module 제외 로직이 PinnedSpace까지 제외하면 **remembered set under-reporting** 발생. PinnedSpace 객체는 remembered set 등록 허용 필요.

### Selective Compaction 시 barrier 변화

```
이동 대상 span의 객체가 이동 중이라면 forwarding pointer 추적 필요.
현재 Value.storage.obj가 raw GcObject*를 직접 저장하므로
  forwarding 없이는 이동 불가 (Stage 7의 선결 문제).
GcMovableBit = 0으로 고정하는 동안은 현재 barrier와 동일.
```

---

## 7. 루트·객체 추적 호환성

### Root 카테고리 (visit_root_references 기준)

| 카테고리 | 코드 위치 | 이동 가능성 |
|---------|-----------|------------|
| `root_environment_` (글로벌 env) | line 3603 | ❌ PinnedSpace |
| `active_environments_` (스택 env) | line 3604–3606 | ❌ old 직접 할당 |
| `rooted_value_vectors_` / `rooted_values_` | line 3607–3618 | N/A (C++ 스택 참조) |
| `active/suspended/retained_coroutines_` | line 3620–3628 | ❌ |
| `interned_strings_` | line 3629–3631 | ⚠️ string pool 도입 시 가능 |
| `native_callback_registry_` | line 3632–3634 | ❌ PinnedSpace |
| `pinned_debug_objects_` | line 3635–3637 | ❌ |
| `modules_` | line 3638–3641 | ❌ PinnedSpace |

### trace() 자식 방문 방식

```
virtual GcObject::trace(Runtime& rt) 구현이 각 타입마다 존재.
trace()는 자식 GcObject*/Value를 rt.mark_value() / rt.mark_object()로 전달.
이 패턴은 space-agnostic → Space 분리 후에도 변경 불필요.

단, selective compaction 도입 시:
  trace()에 update_references() 역할 추가 가능
  → GcObject::update_references(forward_fn) virtual 메서드 (Stage 7)
```

### 카드 메타데이터 보유 타입

**카드 보유 (card-tracked)**:
- `ArrayObject`, `Environment`(Local), `StructInstanceObject`, `EnumInstanceObject`, `CoroutineFrameState`

**카드 미보유 (non-card-tracked)**:
- `ScriptFunctionObject`, `NativeFunctionObject`, `UpvalueCellObject`
- `StructTypeObject`, `EnumTypeObject`, `StringObject`, `ModuleNamespaceObject`

→ 비카드 타입은 young GC 시 `has_direct_young_reference()` 전체 순회 강제. Space 분리 후 non-card span으로 배치하여 별도 처리 가능.

### 이동 가능성 분류

**절대 이동 불가 (PinnedSpace 또는 고정 old)**:
- `CoroutineObject`: C++ 실행 스택이 raw pointer 보유
- `Environment`: `active_environments_` 스택이 raw pointer 보유. 바이트코드 전체가 env pointer로 바인딩 조회
- `root_environment_`, 모듈 env: 글로벌 참조
- `NativeFunctionObject`, `native_callback_registry_` 항목: C++ 함수 포인터 보유
- `host_handles_` 연관 객체: C++ 측 참조 가능성
- `interned_strings_`: `unordered_map<string, StringObject*>` 키가 raw ptr

**이동 불가, 독립 Space 분리 가능**:
- `StructTypeObject`, `EnumTypeObject`: 타입 메타데이터. 런타임 중 고정
- `ModuleNamespaceObject`: 모듈 등록 후 고정

**잠재적으로 이동 가능 (향후 GcMovableBit 적용 후)**:
- `ArrayObject`, `StructInstanceObject`, `EnumInstanceObject`: Value[] 구조가 단순하다면 이동 후 카드 재구성으로 가능
- `StringObject`: `std::string value`가 독립적. 객체 이동 후 string data 유지

**핸들 간접 없이는 이동 불가**:
- `Value.storage.obj`가 raw `GcObject*`를 저장하므로, `ArrayObject::elements`, `Environment::values`, `CoroutineFrameState::stack/locals` 등 **Value를 보유하는 모든 컨테이너**가 포인터 갱신 없이 이동 불가.
- 선택적 compaction은 Value가 참조하지 않는 isolated 객체에만 적용 가능 (현재 거의 없음).

---

## 8. 기존 코드 기반 자료구조 제안

### HeapSpace 베이스 추상화

```cpp
// 신규. Runtime 멤버로 소유.
class HeapSpace {
public:
    virtual ~HeapSpace() = default;
    virtual GcObject*  try_alloc(std::size_t size, ObjectKind kind) = 0;
    virtual void       free_object(GcObject* obj) = 0;
    virtual void       for_each_object(std::function<void(GcObject*)>) = 0;
    virtual void       begin_cycle() = 0;   // color reset
    virtual void       sweep(Runtime& rt, std::size_t& budget) = 0;
    virtual std::size_t live_bytes() const = 0;
    virtual std::size_t used_bytes() const = 0;
    virtual void       verify(GcVerifyContext&) = 0;

    GcSpaceKind space_kind;
};

// Runtime에 추가:
// std::unique_ptr<NurserySpace>    nursery_;
// std::unique_ptr<OldSmallSpace>   old_small_;
// std::unique_ptr<LargeObjectSpace> los_;
// std::unique_ptr<PinnedSpace>     pinned_;
// std::vector<HeapSpace*>          all_spaces_;
```

**현재 대체 대상**: `objects_` (line 2558), `live_bytes_` (line 2586), `allocate<T>` 내 삽입 로직.

---

### NurseryChunk / NurserySpace

```cpp
struct NurseryChunk {
    static constexpr size_t kChunkSize = 256 * 1024;
    std::byte*    base;
    std::byte*    bump_ptr;
    std::byte*    limit;
    NurseryChunk* next_chunk;

    bool      can_alloc(size_t size) const { return bump_ptr + size <= limit; }
    GcObject* alloc(size_t size) {
        GcObject* obj = reinterpret_cast<GcObject*>(bump_ptr);
        bump_ptr += align_up(size, alignof(GcObject));
        return obj;
    }
};

class NurserySpace : public HeapSpace {
    NurseryChunk* current_chunk_;
    NurseryChunk* chunk_list_;
    size_t        used_bytes_      = 0;
    size_t        committed_bytes_ = 0;

    GcObject* try_alloc(size_t size, ObjectKind) override;  // bump fast path
    void      sweep(Runtime& rt, size_t& budget) override;  // age/promote/reset
};
```

**현재 대체 대상**: `allocate<T>` 내 young 경로의 `new T()`, `sweep_young_objects()`, `young_allocation_pressure_bytes_`.

---

### Span / SizeClass / SpanList (OldSmallSpace)

```cpp
// size class 테이블 (GcObject 크기 실측 후 결정)
constexpr size_t kSizeClasses[]  = { 32, 48, 64, 80, 96, 128, 192, 256 };
constexpr int    kNumSizeClasses = 8;

struct Span {
    static constexpr size_t kSpanSize = 4096;
    std::byte* page_base;
    uint8_t    size_class_idx;
    uint16_t   slot_size;
    uint16_t   total_slots;
    uint16_t   live_slots = 0;
    uint64_t   live_bitmap[...];
    uint64_t   free_bitmap[...];
    Span*      next_span;
    Span*      prev_span;
};

struct SizeClassList {
    Span* partial_spans = nullptr;
    Span* full_spans    = nullptr;
    Span* empty_spans   = nullptr;
};

class OldSmallSpace : public HeapSpace {
    SizeClassList size_classes_[kNumSizeClasses];
    size_t live_bytes_ = 0;

    GcObject* try_alloc(size_t size, ObjectKind) override;
    void      sweep(Runtime& rt, size_t& budget) override;
    void      sweep_span(Span* span, size_t& budget);
};
```

> ⚠️ GcObject 서브클래스의 소멸자가 내부 `std::vector/map`을 해제하므로, OldSmallSpace::free_object()는 `delete obj` 대신 `obj->~GcObject()` (소멸자 호출) + slab 슬롯 bitmap 마킹 패턴 사용.

---

### LargeObjectNode / LOS

```cpp
struct LargeObjectNode {
    GcObject*       object;
    size_t          alloc_size;   // page 배수
    LargeObjectNode* next;
    LargeObjectNode* prev;
};

class LargeObjectSpace : public HeapSpace {
    LargeObjectNode* los_list_    = nullptr;
    LargeObjectNode* free_blocks_ = nullptr;  // coalescing 대상
    size_t           live_bytes_  = 0;

    GcObject* try_alloc(size_t size, ObjectKind) override;  // page-aligned
    void      sweep(Runtime& rt, size_t& budget) override;
    void      coalesce_free_blocks();  // VA 연속성 기반 병합
};
```

> `is_large_object()` (line 3652)가 이미 존재하므로 LOS 라우팅 조건은 코드베이스에 준비되어 있음.

---

### PinnedSpace

```cpp
class PinnedSpace : public HeapSpace {
    struct PinnedPage {
        std::byte* base;
        size_t     used;
        size_t     capacity;
        PinnedPage* next;
    };
    PinnedPage* pages_      = nullptr;
    size_t      live_bytes_ = 0;

    GcObject* try_alloc(size_t size, ObjectKind) override;  // bump per-page
    void      sweep(Runtime& rt, size_t& budget) override;  // color 리셋만
    void      explicit_free(GcObject* obj);                  // 명시적 unpinning 시
};
```

---

### AllocationContext (fast-path)

```cpp
// Runtime 내 인라인 캐시 (single-threaded이므로 thread-local 불필요)
struct AllocationContext {
    NurseryChunk* current_nursery_chunk;
    Span*         current_spans[kNumSizeClasses];
    size_t        nursery_trigger;
    size_t        full_trigger;
};
// allocate<T>의 pressure 체크를 AllocationContext로 추상화
// → Space 분리 시 함수 시그니처 변경 없이 내부만 교체 가능
```

---

## 9. 단계별 마이그레이션 계획

### Stage 1: HeapSpace 추상화 도입 (동작 변경 없음)

**목표**: 기존 단일 리스트를 감싸는 `HeapSpace` 인터페이스 도입. 동작 동일.

**변경 대상**:
- `GcHeader`에 `GcSpaceKind space_kind` + `uint8_t size_class` 필드 추가
- `HeapSpace` 순수 가상 베이스 클래스 작성
- `LegacyHeapSpace : HeapSpace` 구현 → 기존 `objects_` 리스트 래핑
- `Runtime`에 `LegacyHeapSpace legacy_heap_` + `std::vector<HeapSpace*> all_spaces_` 추가
- `allocate<T>`: `legacy_heap_.try_alloc()` 경유 (동작 동일)

**보존할 불변식**: `objects_` 존재 유지. 모든 기존 테스트 통과.
**검증**: `LegacyHeapSpace::verify()`가 `gc_verify_full()` 리스트 순회와 동일 결과 산출.

---

### Stage 2: LargeObjectSpace 분리

**목표**: size >= 4KB 객체를 별도 LOS로 라우팅.

**변경 대상**:
- `LargeObjectSpace` 구현 (LargeObjectNode + free block list)
- `allocate<T>` line 2288: `size >= large_object_threshold_bytes_` 분기에서 `los_.try_alloc()` 호출
- `sweep()` 단계에서 `los_.sweep()` 별도 호출
- `begin_gc_cycle()`의 color 초기화에 `los_.begin_cycle()` 추가
- `gc_verify_full()`의 순회에 `los_` 추가
- `ZephyrGcStats::large_objects/large_object_bytes` → `los_` 기반 계산

**검증**: 4KB 이상 객체의 `space_kind == LargeObject` assert. LOS 해제 후 `live_bytes_` 정확성.

---

### Stage 3: PinnedSpace 분리

**목표**: 영구 루트 객체들을 PinnedSpace로 이전. `GcPinnedBit` 실제 활성화.

**변경 대상**:
- `PinnedSpace` 구현
- `pinned_debug_objects_`, `root_environment_`, `modules_` env, `interned_strings_`, `native_callback_registry_` → PinnedSpace 할당
- `GcPinnedBit` 실제 설정 (현재 dead)
- `remember_minor_owner()` line 3978의 Root/Module 제외 로직 재검토 → PinnedSpace 객체 remembered set 등록 허용
- `PinnedSpace::sweep()`: color 리셋만 수행

**검증**: PinnedSpace → Nursery 참조가 `remembered_objects_`에 등록됨 확인.

---

### Stage 4: OldSmallSpace (Span/SizeClass) 도입

**목표**: 일반 old 객체를 size class span 기반 할당으로 이전.

**변경 대상**:
- `Span`, `SizeClassList`, `OldSmallSpace` 구현
- `kSizeClasses` 결정: 실제 GcObject 서브클래스 크기 실측 후 설정
- `allocate<T>`: `is_old && !large` → `old_small_.try_alloc(sizeof(T), kind)`
- `GcHeader::size_class` 설정
- `sweep()` 내 old 객체 처리 → `OldSmallSpace::sweep_spans()`
- `process_detach_queue()`: `obj->header.space_kind` 확인 후 space-aware free
- `rebuild_minor_remembered_set()` / `count_remembered_cards()`: `old_small_.for_each_object()` 사용

**호환 심**: `LegacyHeapSpace`를 old small + young 혼합 모드로 유지하다가 타입별로 점진 이전.

**검증**: Span bitmap 정확성: `live_slots == popcount(live_bitmap)`. empty span 재사용.

---

### Stage 5: NurserySpace (Bump-pointer chunk) 도입

**목표**: Young 객체를 bump-pointer nursery chunk로 이전.

**변경 대상**:
- `NurseryChunk`, `NurserySpace` 구현
- `allocate<T>`: `!should_allocate_old` → `nursery_.try_alloc(sizeof(T))`
  - fast path: `nursery_.current_chunk_->can_alloc(size) ? bump alloc : slow path`
- `sweep_young_objects()` → `NurserySpace::sweep()`
- `perform_young_collection()`의 color 초기화 → `NurserySpace::begin_cycle()`
- `young_allocation_pressure_bytes_` → `nursery_.used_bytes()` 대체

**주의**: Nursery 내 객체 선형 순회 시 `GcHeader::size_bytes` 활용. NurserySpace 내부적으로 `next_all` 임시 유지.

**검증**: young GC 반복 시 survive rate 및 promote 정확성. ASAN으로 청크 reset 후 포인터 접근 없음 확인.

---

### Stage 6: `next_all` 제거 및 per-space iteration 완성

**목표**: 모든 전역 `objects_` 순회를 space API로 대체 후 `next_all` 제거.

**변경 대상 (14개 위치)**:
```
- allocate<T> line 2294–2295 → space.insert(obj)
- Runtime::~Runtime() line 3549–3553 → for each space: space.destroy_all()
- begin_gc_cycle() line 8144–8148 → for each space: space.begin_cycle()
- rebuild_minor_remembered_set() line 4162–4193 → for each space: for_each_object()
- count_remembered_cards() line 4099–4132 → for each space: count_cards()
- sweep_cursor_ 초기화 line 8179 → per-space sweep cursor
- sweep() 체인 순회 → OldSmallSpace::sweep_spans()
- sweep_young_objects() 체인 순회 → NurserySpace::sweep()
- gc_verify_full/young → for each space: space.verify()
```

**GcHeader `next_all` 제거 후**:
```cpp
struct GcHeader {           // ~20B (-8B vs 현재)
    uint32_t    size_bytes;
    uint16_t    type_id;
    GcColor     color;
    uint8_t     flags;
    uint8_t     age;
    GcSpaceKind space_kind;
    uint8_t     size_class;
    uint8_t     padding;
    GcObject*   next_gray;  // 유지
};
```

**검증**: 전체 space `live_bytes` 합 == 이전 단일 `live_bytes_`. gc_stress 장기 안정성.

---

### Stage 7: 선택적 Compaction 훅 (선택적, 향후)

**목표**: 실제 이동 없이 GcMovableBit 경로 준비.

- `GcMovableBit` 활성화 경로 준비
- `Span::is_compactable()` API 추가
- `GcObject::update_references(forward_fn)` virtual 메서드 (stub)
- per-span fragmentation ratio 측정

> 실제 이동을 위해서는 `Value.storage.obj` raw ptr → handle index 전환이 선결 조건.

---

## 10. 리스크 레지스터

### R1: Space 분리 중 객체 누락 (Lost Objects)

| 항목 | 내용 |
|------|------|
| **원인** | `allocate<T>` 전환 과도기에 객체가 어느 space에도 미등록 |
| **탐지** | `gc_verify_full()` reachability 실패. `DEBUG_LEAK_CHECK` 카운터 불일치 (line 159–168) |
| **완화** | Stage 진입 전 `gc_verify_full()` 통과 확인. `LegacyHeapSpace`를 기준선으로 유지 |

### R2: Gray Stack 불변식 파괴

| 항목 | 내용 |
|------|------|
| **원인** | `begin_gc_cycle()`이 일부 space를 누락하면 해당 space 객체가 Black으로 남아 영구 수집 불가 |
| **탐지** | `gc_verify_full()` reachability 실패. gc_stress 모드에서 `live_bytes_` 지속 증가 |
| **완화** | `HeapSpace::begin_cycle()` 패턴으로 `all_spaces_` 등록 space 전체 커버. Space 추가 시 자동 등록 강제 |

### R3: Remembered Set Under-Reporting

| 항목 | 내용 |
|------|------|
| **원인 1** | Stage 3 (PinnedSpace) 시 `remember_minor_owner()` (line 3978) Root/Module 제외가 PinnedSpace까지 제외 |
| **원인 2** | Stage 4 (OldSmallSpace) 시 `rebuild_minor_remembered_set()` 순회가 OldSmallSpace 미커버 |
| **탐지** | `gc_verify_young()` 검사 실패. Young 객체 수집 후 dangling pointer → ASAN crash |
| **완화** | `remember_minor_owner()`의 제외를 `EnvironmentKind::Root`에만 한정. `gc_verify_young()`에 PinnedSpace→Nursery 참조 확인 추가 |

### R4: Promotion 버그

| 항목 | 내용 |
|------|------|
| **원인** | Stage 5 (NurserySpace) 시 `promote_object()`가 NurseryChunk 내 객체를 OldSmallSpace로 이동할 때 청크 reset으로 이중 접근 |
| **탐지** | ASAN use-after-free |
| **완화** | promote 전 nursery reset 지연. 또는 OldSmallSpace에 placement + memcpy 후 청크 reset |

### R5: Finalizer 순서 회귀

| 항목 | 내용 |
|------|------|
| **원인** | Space 분리 후 sweep 순서 변경 → `detach_queue_` 삽입 순서 변경 → finalizer 실행 순서 변경 |
| **탐지** | finalizer 순서 의존 테스트 실패 |
| **완화** | finalizer 순서 보장 없음을 문서화. 필요 시 detach_queue_를 per-space로 분리하고 순서 고정 |

### R6: 계정 불일치 (Accounting Mismatch)

| 항목 | 내용 |
|------|------|
| **원인** | `live_bytes_` 감산이 sweep (8220), process_detach_queue (8240), sweep_young_objects (8763, 8783)에 분산. Space별 전환 시 중복 또는 누락 |
| **탐지** | `ZephyrGcStats::live_bytes`가 `sum(per-space live_bytes)`와 불일치 |
| **완화** | `HeapSpace::free_object()` 내부에서만 `live_bytes_` 감산. Runtime의 `live_bytes_`를 `sum(spaces)` 계산으로 대체. 전환 중 양쪽 assert 비교 |

### R7: 단편화 지표 오측

| 항목 | 내용 |
|------|------|
| **원인** | `size_bytes = sizeof(T)` (line 2283)이 내부 `std::vector`, `unordered_map` 등 nested heap 미포함 |
| **탐지** | `mallinfo2()` 또는 `/proc/self/status` RSS와 `live_bytes` 비교 |
| **완화** | GcObject 서브클래스에 `virtual size_t deep_size() const` 추가. 통계에 `shallow_bytes`와 `deep_bytes` 구분 |

### R8: Pinned 객체가 Movable Span에 혼입

| 항목 | 내용 |
|------|------|
| **원인** | Stage 4/7에서 `GcMovableBit` span에 `GcPinnedBit` 객체가 혼입 → compaction 시 이동 시도 |
| **탐지** | compaction 후 pinned 객체 주소 변경 → C++ 코드 crash |
| **완화** | `OldSmallSpace::alloc_pinned(size)` 전용 함수로 pinned-only span에만 할당. `Span::is_pinned_span` 플래그. compaction 시 skip |

### R9: LOS Coalescing 오류

| 항목 | 내용 |
|------|------|
| **원인** | 인접 free block 병합 시 VA 불연속 블록을 VA 연속으로 오판 → 살아있는 객체 메모리 덮어쓰기 |
| **탐지** | ASAN heap 검사 |
| **완화** | coalescing을 VA 연속성 기반으로만 수행. `base + alloc_size == other->base` 확인. `LargeObjectNode`를 VA 주소 순으로 정렬된 별도 sorted list에 유지 |

### R10: Debug Verify Blind Spot

| 항목 | 내용 |
|------|------|
| **원인** | `gc_verify_full()` (line 4306+)과 `gc_verify_young()` (line 8849+)이 `objects_` 단일 순회 기반. LOS/PinnedSpace 객체 누락 |
| **탐지** | 검증 통과하지만 LOS 객체가 dangling pointer 보유 |
| **완화** | 각 Stage에서 해당 space를 verify에 추가. `HeapSpace::verify(VerifyContext&)` 패턴으로 항상 `all_spaces_` 전체 커버 |

---

## 11. 계측 계획

### per-Space 통계 (ZephyrGcStats 확장)

```cpp
// include/zephyr/api.hpp ZephyrGcStats 확장 (현재 lines 273-303)
struct ZephyrSpaceStats {
    std::size_t live_bytes          = 0;
    std::size_t used_bytes          = 0;   // live + 내부 단편화
    std::size_t reserved_bytes      = 0;   // OS committed
    std::size_t object_count        = 0;

    // OldSmallSpace 전용
    std::size_t full_spans          = 0;
    std::size_t partial_spans       = 0;
    std::size_t empty_spans         = 0;
    std::size_t span_fragmentation_pct = 0;  // (used - live) * 100 / used

    // LOS 전용
    std::size_t largest_free_block_bytes = 0;
    std::size_t free_block_count         = 0;

    // NurserySpace 전용
    std::size_t bump_ptr_offset     = 0;
    std::size_t chunk_count         = 0;
};

struct ZephyrGcStats {
    // 기존 유지...

    ZephyrSpaceStats nursery;
    ZephyrSpaceStats old_small;
    ZephyrSpaceStats large_object;
    ZephyrSpaceStats pinned;

    // 신규 통합 지표
    std::size_t heap_fragmentation_pct      = 0;  // (total_used - live) * 100 / total_used
    std::size_t nursery_survival_bytes      = 0;
    std::size_t nursery_survival_pct        = 0;
    std::size_t promotion_bytes_last_frame  = 0;
    std::size_t pinned_bytes_ratio_pct      = 0;  // pinned / total_live * 100
};
```

### 검증 Assertion 추가

```cpp
// 모든 live object가 정확히 하나의 space에 속하는지 확인
void VerifyContext::assert_space_coverage() {
    std::unordered_set<GcObject*> seen;
    for (auto& space : all_spaces_)
        space.for_each_object([&](GcObject* o) {
            assert(seen.insert(o).second && "object in multiple spaces");
            assert(o->header.space_kind == space.space_kind);
        });
}

// GcHeader::space_kind ↔ GcOldBit 일관성
void VerifyContext::assert_space_kind_consistent(GcObject* obj) {
    assert((obj->header.flags & GcOldBit) ==
           (obj->header.space_kind != GcSpaceKind::Nursery) &&
           "GcOldBit and space_kind disagree");
    if (obj->header.space_kind == GcSpaceKind::LargeObject)
        assert(obj->header.size_bytes >= large_object_threshold_bytes_);
}

// live_bytes 일치 확인
void Runtime::assert_accounting_consistent() {
    std::size_t computed = 0;
    for (auto& space : all_spaces_) computed += space->live_bytes();
    assert(computed == live_bytes_);
}

// remembered set 완전성 확인 (gc_verify_young 강화)
void Runtime::assert_remembered_set_complete() {
    for (auto* space : {(HeapSpace*)old_small_.get(), los_.get(), pinned_.get()})
        space->for_each_object([&](GcObject* o) {
            if (has_direct_young_reference(o))
                assert((o->header.flags & GcMinorRememberedBit) &&
                       "old->young ref not in remembered set");
        });
}
```

### gc_verify_full / gc_verify_young 업데이트

```cpp
// 변경 후 gc_verify_full():
VoidResult Runtime::gc_verify_full() {
    GcVerifyContext ctx{...};
    if (legacy_heap_) legacy_heap_->verify(ctx);  // Stage 1
    if (los_)         los_->verify(ctx);           // Stage 2
    if (pinned_)      pinned_->verify(ctx);        // Stage 3
    if (old_small_)   old_small_->verify(ctx);     // Stage 4
    if (nursery_)     nursery_->verify(ctx);       // Stage 5
    assert_space_coverage();                        // Stage 6 활성화
    assert_accounting_consistent();
    assert_remembered_set_complete();
    return ctx.result();
}
```

---

## 12. 구체적 TODO 체크리스트

### [GC Core]
- [ ] `GcHeader` (zephyr.cpp:1698–1706): `uint8_t space_kind` + `uint8_t size_class` 필드 추가
- [ ] `GcFlags` enum (zephyr.cpp:1683): `GcMovableBit = 1 << 6` 추가. `GcPinnedBit` 실제 사용 로직 연결
- [ ] `HeapSpace` 순수 가상 베이스 클래스 신설 (`for_each_object`, `begin_cycle`, `sweep`, `live_bytes`, `verify`)
- [ ] `Runtime::all_spaces_` (`std::vector<HeapSpace*>`) 멤버 추가
- [ ] `Runtime::begin_gc_cycle()` (zephyr.cpp:8137): `for (auto s : all_spaces_) s->begin_cycle()` 리팩터
- [ ] `gc_step()` (zephyr.cpp:8935): SweepObjects 단계에서 space별 sweep 직접 호출

### [Allocation]
- [ ] `Runtime::allocate<T>()` (zephyr.cpp:2281–2309): space 라우팅 (`nursery_` / `old_small_` / `los_` / `pinned_`). Stage 1: `LegacyHeapSpace`로 래핑만
- [ ] `Runtime::should_allocate_old()` (zephyr.cpp:3644): large object 조건을 `is_large` + `is_direct_old`로 분리
- [ ] `AllocationContext` 구조체 신설: nursery bump cursor, per-size-class span cursor
- [ ] `NurseryChunk` + `NurserySpace::try_alloc()` 구현
- [ ] `OldSmallSpace::try_alloc(size, kind)` + size class 결정 함수 구현
- [ ] `LargeObjectSpace::try_alloc(size)` 구현 (page-aligned)
- [ ] `PinnedSpace::try_alloc(size)` 구현
- [ ] `GcHeader::space_kind` 설정을 `allocate<T>` 내 space 결정 시점에 즉시 수행

### [Promotion]
- [ ] `Runtime::promote_object()` (zephyr.cpp:3656–3677): OldSmallSpace 또는 PinnedSpace로 이동 로직 추가
- [ ] `rebuild_minor_remembered_set()` (zephyr.cpp:4161): `for (auto s : all_spaces_) s->for_each_object(...)` 로 교체 (Stage 6)
- [ ] `has_direct_young_reference()` (zephyr.cpp:~3955): promote 시 전체 순회 제거. write barrier 자동 등록으로 대체

### [Barrier]
- [ ] `note_write(GcObject*, const Value&)` (zephyr.cpp:8276): `is_old_object()` → `space_kind != Nursery`로 전환 (Stage 6)
- [ ] `note_write` 양쪽 오버로드 (8258, 8276): LOS, PinnedSpace 객체 → Nursery 참조 시 `remembered_objects_` 등록 허용
- [ ] `remember_minor_owner()` (zephyr.cpp:3972): Root/Module 제외 로직을 PinnedSpace 객체까지 제외하지 않도록 수정
- [ ] Card table: `vector<uint8_t>` → `vector<uint64_t>` bitmap 교체. `owner_has_dirty_minor_cards()`에 `__builtin_ctzll` 활용

### [Tracing]
- [ ] `GcObject::trace(Runtime&)`: space-agnostic. 유지
- [ ] `Runtime::drain_gray()` (zephyr.cpp:8184): space-agnostic. 유지
- [ ] `Runtime::trace_young_references()` (zephyr.cpp:3679): `space_kind` 기반으로 재작성
- [ ] `owner_is_fully_card_tracked()` (zephyr.cpp:3771): space 속성 기반으로 재구현 여부 검토

### [Sweep/Reclaim]
- [ ] `Runtime::sweep()` (zephyr.cpp:8201): `delete current` (line 8221) → `current->get_space()->free_object(current)` 교체
- [ ] `Runtime::sweep_young_objects()` (zephyr.cpp:8743): `NurserySpace::sweep()` 으로 이전
- [ ] `Runtime::process_detach_queue()` (zephyr.cpp:8236): `delete object` → `object->get_space()->free_object(object)` 교체
- [ ] `Runtime::~Runtime()` (zephyr.cpp:3548): `for (auto s : all_spaces_) s->destroy_all()` 교체
- [ ] `OldSmallSpace::sweep_span()`: bitmap 기반 dead-slot 마킹 + empty span 재활용
- [ ] `LargeObjectSpace::sweep()` + `coalesce_free_blocks()` 구현

### [Verification]
- [ ] `gc_verify_full()` (zephyr.cpp:4306+): `HeapSpace::verify()` 순회 패턴으로 전환. `assert_space_coverage()` 추가
- [ ] `gc_verify_young()` (zephyr.cpp:8849+): `NurserySpace::verify()` + `assert_remembered_set_complete()` 추가
- [ ] `assert_accounting_consistent()`: `sum(space->live_bytes) == live_bytes_` 매 GC 사이클 후 실행
- [ ] `GcPinnedBit` 활성화 검증: PinnedSpace 객체에 `GcPinnedBit` 설정됨 assert

### [Metrics]
- [ ] `ZephyrGcStats` (api.hpp:273): `ZephyrSpaceStats nursery/old_small/large_object/pinned` 필드 추가
- [ ] `heap_fragmentation_pct`, `nursery_survival_pct`, `promotion_bytes_last_frame`, `pinned_bytes_ratio_pct` 추가
- [ ] `count_remembered_cards()` (zephyr.cpp:4097): `objects_` 전순회 → `for (auto s : all_spaces_) s->count_cards()` 합산
- [ ] `ZephyrGcStats::large_objects/large_object_bytes` (api.hpp:281): `los_` 내부 집계로 전환
- [ ] GcObject 서브클래스에 `virtual size_t deep_size() const` 추가 (nested heap 포함)

### [Tests]
- [ ] 각 Stage 진입 전: `gc_verify_full()` + `gc_verify_young()` 전수 통과 확인
- [ ] `gc_stress_enabled_ = true` + Stage별 회귀 테스트 (zephyr.cpp:8249–8256 safe point 활용)
- [ ] LOS: 4KB 이상 ArrayObject 반복 alloc/free → coalescing 검증
- [ ] OldSmallSpace: 동일 size class 객체 반복 alloc → span partial/full/empty 전이 검증
- [ ] NurserySpace: young GC 반복 시 survive rate 및 promote 정확성 검증
- [ ] PinnedSpace → Nursery inter-gen 참조: remembered set 등록 확인
- [ ] `assert_accounting_consistent()`: 전 Stage에 걸쳐 매 GC 사이클 후 실행
- [ ] `DEBUG_LEAK_CHECK` 카운터 (zephyr.cpp:159–168): Stage 전환 전후 `created - destroyed == live count` 일치 확인

---

*Generated: 2026-03-22 | Based on: `src/zephyr.cpp` (10,231 lines), `include/zephyr/api.hpp`*
