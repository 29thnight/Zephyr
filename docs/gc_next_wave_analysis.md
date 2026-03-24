# GC Next-Wave 분석: Post-Phase-6 현황 및 위험 평가

> **상태**: 작성일 2026-03-22 — Phase 4A~6 완료 직후
> **목적**: 다음 구현 Wave 시작 전 현황 파악, 잠재 버그 식별, 우선순위 결정
> **구현 계획**: `docs/gc_next_wave_plan.md` (이 파일에서 도출)

---

## 1. 현재 상태 해석

### 코드에서 이미 참인 것

4-space heap은 완전히 배선되어 활성화되어 있다. `LegacyHeapSpace`는 폴백 경로 없이 완전히 제거되었다. `allocate<T>()`로 할당되는 모든 객체는 크기와 연령 휴리스틱에 따라 `nursery_`, `old_small_`, `los_` 중 정확히 하나로 라우팅된다. `pinned_`는 `allocate_pinned<T>()`로만 채워진다. `all_spaces_` 벡터가 `begin_cycle()`, `for_each_object()`, `sweep()`을 균일하게 구동한다. `accounting_consistent()`는 모든 `Complete` 전환마다 4-way 합산을 검증하며 assert된다. `ZephyrSpaceStats`는 `runtime_stats()`에서 per-space `live_bytes`와 `object_count`로 채워진다. `begin_sweep()`이 `process_dirty_roots()` → `SweepObjects` 전환 시 `nursery_`와 `old_small_` 양쪽에서 호출되어 마킹 후 White 할당이 해당 사이클의 sweep에 포함되지 않도록 한다.

### 아직 존재하는 위험한 전환 코드

**Sweep 헤드 커럽션 race (잠재, 현 테스트로 미탐지):**
`NurserySpace::sweep()`과 `OldSmallSpace::sweep()` 모두:

```
begin_sweep() → sweep_cursor_ = objects_   (old head)
— GC step 사이에 새 객체 D 가 prepend: objects_ = D → old_head —
sweep 실행: first object (sweep_cursor_) 가 White → objects_ = next
→ D 가 objects_ 에서 끊겨 고아 (memory leak + dangling next_all)
```

`sweep_young_objects()`는 단일 호출(non-incremental)이므로 영향 없음.
버그가 발화하는 조건: `SweepObjects` 단계 사이에 새 객체가 할당되고, sweep이 시작될 때 `sweep_cursor_` 위치의 첫 객체가 White인 경우.

**`process_detach_queue()` Pinned 분기 누락:**
dispatch 순서: `LargeObject` → `OldSmall` → `Nursery` → `else`.
Pinned 객체에 `GcFinalizableBit`이 설정되어 있으면 `else` 폴백 → `live_bytes_` 만 감소하고 `pinned_.live_bytes_`는 미감소 → `accounting_consistent()` 영구 실패.
현재 Pinned 객체가 finalizable하지 않아 테스트 미발화.

**`OldSmallSpace::for_each_object()`가 빈 슬랩 리스트를 순회:**
Phase 4C가 미활성화되어도 `kOssClassCount`개의 `SizeClassList`를 순회. `begin_gc_cycle()`, `rebuild_minor_remembered_set()` (2-pass), `gc_verify_full()`, `runtime_stats()` 등 모든 호출마다 불필요한 반복.

**`GcHeader::size_class`가 항상 0:**
`allocate<T>()`가 `old_small_.insert()`로 라우팅하지만 `try_alloc_slab()`을 호출하지 않고 `header.size_class`를 설정하지 않음. 필드 존재하나 정보 미보유.

**`LargeObjectSpace`는 `begin_cycle()`에서 sweep cursor 초기화 (의도적 비대칭):**
LOS는 `begin_cycle()`에서 `sweep_cursor_ = head_`를 설정하고 `begin_sweep()`이 없음.
Nursery/OldSmall과 달리 LOS는 prepend 방식이므로 마킹 중 새 객체가 head에 삽입되어도 이전 cursor에서 순방향 순회 시 도달 불가 → race 없음. 의도적이나 문서화 필요.

**REFACTOR-1B/1C/SWEEP 태그:**
이미 해결된 refactor의 고고학적 마커. 미정리 시 혼란 유발.

### "Deferred by design"의 구현상 의미

**Phase 4C (슬랩):** `try_alloc_slab()`, `free_slab_object()`, `SizeClassList`, `GcSpan` 모두 컴파일됨. `allocate<T>()`는 결코 `try_alloc_slab()`을 호출하지 않음. 슬랩 경로는 완전한 dead code.

**Phase 5C (범프):** `NurserySpace`에 청크나 아레나 없음. 모든 nursery 객체가 개별 `new T()`로 할당됨. 범프 포인터, 청크 메타데이터, 리필 로직 없음.

**Phase 7 (컴팩션):** 포워딩 포인터 필드 없음. 이동 가능/불가능 분류 없음. 핸들 간접 참조 레이어 없음. Value가 전체에 걸쳐 raw `GcObject*` 포인터 보유.
단, `GcMovableBit = 1 << 6`이 `GcHeader::flags`에 이미 예약되어 있음.

---

## 2. 즉각 감사 대상

### 2.1 할당 라우팅

**위험 이유:** 세 가지 경로(nursery, old_small, los)에 추가 pinned fast path. 잘못된 라우팅은 `space_kind`를 오염시켜 detach queue dispatch, `accounting_consistent()`, `verify()`, `for_each_object()`를 모두 오염.

**검사할 것:** `allocate<T>()` — `should_allocate_old()`가 nursery에 있어야 할 타입에 대해 true를 반환하지 않는지 확인. `header.space_kind`가 각 space의 `insert()` 호출 전에 설정되는지 확인. 현재 코드:
```cpp
bool Runtime::should_allocate_old(ObjectKind kind, std::size_t size_bytes) const {
    (void)size_bytes;
    return kind == ObjectKind::Coroutine || kind == ObjectKind::Environment;
}
```

**성공 기준:** `allocate<T>()`가 반환하는 모든 객체의 `space_kind`가 삽입된 space와 일치하고, `GcOldBit` 상태가 old-generation 여부와 일치.

### 2.2 Sweep 및 회수 경로

**위험 이유:** §1에서 설명한 헤드 커럽션 race가 `NurserySpace::sweep()`과 `OldSmallSpace::sweep()` 모두에 존재. 또한 `NurserySpace::sweep()`(풀 GC 경로)와 `sweep_young_objects()`(young GC 경로)가 동일한 `objects_` 리스트를 두 개의 별도 구현으로 sweep.

**검사할 것:**
- `NurserySpace::sweep()`: `sweep_previous_ == nullptr`이고 `sweep_cursor_`의 객체가 White인 경우 추적. `objects_`에 무슨 일이 일어나는지, `begin_sweep()` 이후 prepend된 객체가 이후에도 도달 가능한지 확인.
- `sweep_young_objects()`: `sweep_cursor_` 또는 `sweep_previous_`를 읽거나 수정하지 않음을 확인 (incremental sweep 기계를 사용하지 않음).

**성공 기준:** `begin_sweep()` 이후 삽입된 객체가 sweep 사이클 종료 후 `objects_`에서 도달 가능.

### 2.3 Remembered set 정확성

**위험 이유:** `rebuild_minor_remembered_set()`이 `for_each_object`를 통해 모든 space를 순회. Phase 5B+6 이후 nursery와 old_small 모두 순회됨. 카드 테이블 로직은 단일 `objects_` 체인이 있을 때 작성됨. 이제 old 객체가 `old_small_` 또는 `los_`에 있을 수 있음. promotion이 `rebuild_environment_cards()` 등을 호출하지만 이 함수들은 `space_kind`를 확인하지 않음.

**검사할 것:**
- `promote_object()`: 카드 재빌드 호출이 무조건 발화하는지 확인.
- `gc_verify_young()`: remembered-set 검증이 `old_small_`과 `los_`의 객체를 포함하는지 확인.

**성공 기준:** promotion 이후, 라이브 nursery 참조를 보유한 경우에만 promoted 객체가 `remembered_objects_`에 나타남.

### 2.4 Per-space accounting

**위험 이유:** 4개의 독립 `live_bytes_` 카운터가 각각 `live_bytes_` 전역에 포인터로 바인딩. 이중 감소, 누락 감소, accounting 업데이트 없는 `delete`가 `accounting_consistent()`를 깨뜨림.

**검사할 것:**
- `process_detach_queue()`: `else` 폴백. 어떤 타입이 도달할 수 있는지. `pinned_.live_bytes_`를 감소시키는지.
- `sweep_young_objects()`: promoted 객체 경로. `live_bytes_`가 일관성 있게 유지되는지.
- `destroy_all()` 호출 (소멸자): `live_bytes_`를 이중 감소시키지 않는지.

**성공 기준:** `accounting_consistent()`가 space 경계를 넘는 모든 연산(promotion, finalization, manual free) 이후 통과.

### 2.5 Verify/debug 커버리지

**위험 이유:** `gc_verify_full()`이 단일 `objects_` 체인이 있을 때 작성됨. 이제 4개 space를 순회해야 함. 크로스-space 불변식을 검증하지 않을 수 있음.

**검사할 것:**
- `gc_verify_full()`이 4개 space 모두에 대해 `space->verify()`를 호출하고 오류 문자열을 표면화하는지.
- live_bytes 합산 일관성을 검증하는지.
- 두 space에 나타나는 `GcObject*`가 없음을 확인하는지.

### 2.6 Detach/finalization 동작

**위험 이유:** `process_detach_queue()`가 `space_kind`로 dispatch하여 올바른 accounting으로 free. `else` 폴백은 무음 부분 free (전역 카운터 감소, per-space 카운터 미감소).

**검사할 것:**
- `GcSpaceKind` 값 열거: `Nursery(0), OldSmall(1), LargeObject(2), Pinned(3), EnvArena(4), CoroArena(5)`.
- Pinned 분기 누락 확인 (현재 코드에서 확인됨).

### 2.7 Space-kind 일관성

**위험 이유:** `GcHeader::space_kind`가 `GcSpaceKind::OldSmall`로 기본 초기화됨. 초기화를 잊은 새 할당 경로가 있으면 OldSmall로 무음 오분류.

**검사할 것:**
- `GcHeader` 기본값: `space_kind = GcSpaceKind::OldSmall`. 미래 코드에 위험.
- `allocate_pinned<T>()`: `space_kind = GcSpaceKind::Pinned`를 설정하는지.
- `size_class` 필드: 어디서도 설정되지 않음. Phase 4C까지 예약.

### 2.8 Stale helpers 및 dead branches

**위험 이유:** `OldSmallSpace::for_each_object()`가 빈 슬랩 리스트를 순회. REFACTOR 태그가 노이즈. `should_allocate_old()` 주석에 `legacy_heap_` 참조가 있을 수 있음.

---

## 3. 인식된 성능 위험

### 3.1 Nursery당 OS 할당 (영향 가장 큼)

**likely 원인:** Phase 5B가 모든 nursery 객체에 `new T(...)`를 사용. Phase 5C (범프 할당) 미구현.

**측정 방법:** `allocate<T>()` 호출 빈도와 malloc 압력 프로파일링. `total_allocations_ / elapsed_time` 비교.

**최적화 시기:** 5C가 해결책. 하드닝 완료 전 최적화 금지.

### 3.2 promote_object()의 O(n) 비용

**likely 원인:** `nursery_.remove()`가 promoted 객체를 찾기 위해 linked list를 순회. N개 객체를 가진 nursery에서 promotion이 O(N).

**측정 방법:** `total_promotions_ / young_collection_time` 추적. 급증이 이 문제 지시.

**최적화 시기:** Phase 5C (범프 할당) 이후 moot — 범프 할당 객체는 컬렉션 시 청크에서 evacuated. 5C 이전에 linked-list nursery가 있는 동안 significant 병목이 되면 `sweep_young_objects()`의 인라인 순회를 별도 remove 없이 재사용하여 완화 가능.

### 3.3 `OldSmallSpace::for_each_object()` 슬랩 순회 오버헤드

**likely 원인:** 모든 슬랩이 비어있어도 `kOssClassCount`개의 슬랩 리스트를 무조건 순회.

**측정 방법:** `OldSmallSpace::for_each_object()` 호출 횟수와 경과 시간 프로파일링.

**최적화 시기:** H5에서 `slab_active_` 게이트로 해결. Low risk, 작은 변경.

### 3.4 LOS 객체당 `LargeObjectNode` 이중 할당

**likely 원인:** `LargeObjectSpace::insert()`가 삽입된 모든 객체에 `new LargeObjectNode{...}`를 호출. 각 large 객체가 두 번 할당 필요.

**측정 방법:** `LargeObjectNode` 할당 vs LOS 객체 수 카운트 (1:1이어야 함).

**최적화 시기:** Phase 4C follow-on으로 별도 고정 크기 풀에서 `LargeObjectNode`를 슬랩 할당. 현재 critical path 아님.

### 3.5 `rebuild_minor_remembered_set()` 전체 2-pass 비용

**likely 원인:** Pass 1이 모든 space의 모든 live 객체에서 `GcMinorRememberedBit`를 지움. Pass 2가 모든 old 객체에 대해 카드를 재빌드. 두 pass가 모든 live 객체를 터치.

**측정 방법:** `rebuild_minor_remembered_set()`를 전체 GC 시간의 비율로 타이밍.

**최적화 시기:** 아직 아님. 증분 remembered-set 재빌드가 Phase 7+ 관심사. 지금은 전체 재빌드가 정확하고 bounded.

### 3.6 프로덕션 런타임으로 누출되는 Verify 오버헤드

**likely 원인:** `gc_verify_full()`과 `gc_verify_young()`이 `gc_stress` 경로에서 호출됨. gc_stress가 프로덕션에서 실수로 활성화되면 할당마다 전체 verify 실행.

**측정 방법:** 프로덕션 런타임 통계에서 `ZephyrGcStats::gc_stress_enabled` 확인.

**최적화 시기:** `gc_verify_full()`과 `gc_verify_young()`을 `ZEPHYR_DEBUG_GC_VERIFY` 전처리기 매크로로 guard하여 릴리즈 빌드에서 no-op 컴파일.

---

## 4. 크로스-스페이스 정확성 체크리스트

### 객체 소유권 불변식

1. 모든 라이브 `GcObject*`는 정확히 하나의 space가 소유.
2. 객체의 `header.space_kind`는 소유하는 space와 같아야 함.
3. LargeObjectSpace 객체의 `header.next_all`은 `nullptr`이어야 함 (intrusive list link 없음).
4. `header.next_all`은 소유 space의 linked list 내에 있어야 하며 다른 space의 체인을 가리키면 안 됨.

### Accounting 불변식

5. `nursery_.live_bytes() + old_small_.live_bytes() + los_.live_bytes() + pinned_.live_bytes() == live_bytes_` 항상 성립.
6. `space.live_bytes()`가 해당 space의 객체에 대한 `delete object` 전에 감소.
7. Promotion: `nursery_.live_bytes_own_` 감소, `old_small_.live_bytes_` 증가, `live_bytes_` 변경 없음.
8. 어떤 space의 `live_bytes()`도 음수가 되지 않음. 각 감소는 assert로 guard.

### 컬러링 불변식

9. `gc_marking() == true` (SeedRoots, DrainGray, RescanDirtyRoots) 중 새 할당은 `GcColor::Black`이어야 함.
10. `gc_marking() == false` (Idle, SweepObjects, DetachQueue, Complete) 중 새 할당은 `GcColor::White`이어야 함.
11. `begin_gc_cycle()` 종료 시 모든 space의 모든 객체는 `GcColor::White`.
12. Pinned 객체는 매 sweep마다 color가 White로 리셋되지만 절대 free되지 않음.
13. gray stack에 없는 모든 객체는 `next_gray == nullptr`이어야 함.

### 배리어 불변식

14. young 객체 포인터를 old 객체에 쓰는 것은 write barrier를 trigger해야 함.
15. Write barrier는 old owner를 `dirty_objects_` 또는 `dirty_root_environments_`에 정확히 한 번 추가해야 함.
16. `GcDirtyQueuedBit`가 중복 dirty-queue 항목을 방지.
17. `rebuild_minor_remembered_set()` 이후 young 객체를 가리키는 모든 old 객체가 `remembered_objects_`에 있어야 함.
18. young 객체는 `remembered_objects_`에 없어야 함 (기억된 소유자는 항상 old-generation).

### Promotion 불변식

19. `promote_object(obj)` 이후 `obj->header.space_kind == GcSpaceKind::OldSmall`.
20. promotion 이후 `obj`가 `old_small_`의 순회에 나타나고 `nursery_`의 순회에 나타나지 않음.
21. `nursery_.live_bytes_own_`이 `obj->header.size_bytes`만큼 감소하고 `old_small_.live_bytes_`가 같은 양만큼 증가, 같은 호출에서.
22. `GcOldBit`가 `old_small_.insert(obj)` 호출 전에 `obj`에 설정되어야 함.
23. `promote_object()`가 반환 전 모든 관련 객체 종류에 대해 카드 재빌드를 호출해야 함.
24. `promote_object()`가 이미 old 객체에 대해 호출되면 안 됨.

### 회수 불변식

25. 소유 space의 accounting 감소 없이 `delete object` 없음.
26. Pinned 객체에 대한 `delete object` 없음. `PinnedSpace::sweep()`은 color만 리셋.
27. sweep에서 Black 객체는 sweep cursor가 지나치기 전에 color가 White로 리셋되어야 함.
28. sweep에서 White 객체의 `next_all`은 삭제 후 따라가면 안 됨.
29. surviving sweep 체인은 sweep 시점에 Black이었던 모든 객체를 포함해야 함.

### Detach queue 불변식

30. `detach_queue_`의 객체는 dequeue 시점에 유효한 `header.space_kind`를 가져야 함.
31. `detach_queue_`의 객체는 어떤 space의 live 순회에도 있으면 안 됨.
32. dequeue 및 처리 후 space의 `live_bytes_`가 객체 크기만큼 감소해야 함.
33. `GcFinalizableBit`은 finalizer가 호출될 때까지 설정 유지.

### 통계 불변식

34. `stats.gc.nursery.live_bytes == nursery_.live_bytes()` at `runtime_stats()` 호출 시.
35. `stats.gc.nursery.object_count`는 `nursery_.for_each_object()`로 순회 가능한 객체 수와 일치.
36. 다른 세 space도 마찬가지.
37. `stats.gc.live_bytes == live_bytes_` (전역 카운터).
38. `stats.gc.young_bytes == nursery_.live_bytes()` (Phase 6 이후 young bytes는 nursery bytes만).

---

## 5. Verify/Debug 확장 계획

### `gc_verify_full()`에 추가할 항목

**크로스-스페이스 유일성**: 4개 space의 `for_each_object`에 걸쳐 `std::unordered_set<const GcObject*>` 수집. 중복 포인터가 space-membership 버그 신호.

**Per-space live_bytes 일관성**: 4개 space의 합산 vs `live_bytes_`. `accounting_consistent()`의 진단 버전 (어떤 space가 drift인지, 얼마나 drift인지 명시).

**Space-kind membership 확인**: 각 space의 `for_each_object` 순회 중 `object->header.space_kind`가 순회 중인 space와 일치하는지 확인.

**Color 일관성**: `gc_phase_ == Idle` → 모든 객체 White여야 함.

**`next_gray` 청결도**: `gc_phase_ == Idle` → 모든 객체 `next_gray == nullptr`.

**`LargeObjectSpace::verify()` 추가**:
- doubly-linked 무결성: `head_->prev == nullptr`, `tail_->next == nullptr`
- 순방향/역방향 traversal 횟수 일치

**`PinnedSpace::verify()` 추가**:
- Pinned 객체가 gray stack에 없음 확인 (`next_gray == nullptr`)

### Remembered-set 완전성 검증

`gc_verify_full()` (not just `gc_verify_young()`)에서:
`old_small_`과 `los_`의 모든 old 객체에 대해 nursery 객체를 참조하면 `remembered_objects_`에 있어야 함. `rebuild_minor_remembered_set()`와 같은 코드 경로로 각 old 객체의 내용을 순회하여 교차 확인.

### 고아 객체 탐지 (Phase 5C 선행 작업)

범프 할당이 활성화되면: 모든 nursery 청크의 모든 바이트를 순회하여 라이브 `GcObject`처럼 보이는 slot 식별. 모든 그런 객체가 `nursery_.for_each_object()`에서 도달 가능한지 확인.

### 기존 verify 로직의 편향

- 어떤 루프가 `GcOldBit` 확인으로 young 객체를 식별하는지 — Phase 6 이후 정확하지만 명시적으로 문서화 필요.
- `gc_verify_young()`의 remembered-set 검증이 `is_old_object(object)`를 각 remembered owner에 대해 확인 — `OldSmall ∪ LOS ∪ Pinned`를 비트로 확인함을 주석으로 명확히.

---

## 6. 컴팩션 준비 훅 (Phase 7, 움직임 미구현)

### 현재 코드에 이미 있는 것 (확인됨)

- `GcMovableBit = 1 << 6` in `GcHeader::flags` — 이미 예약됨
- `GcSpaceKind::EnvArena(4)`, `CoroArena(5)` — 이미 예약됨

### 추가할 메타데이터 훅

- `GcObject::is_movable()` 가상 메서드 (기본값 false)
- `GcObject::update_internal_pointers(GcObject* new_self)` 가상 no-op
- `HeapSpace::can_compact()` 가상 메서드 (기본값 false)

### Value 구조체의 위험

컴팩션의 가장 큰 blocker는 `Value`가 raw `GcObject*` 포인터를 저장한다는 것. 모든 콜 스택의 `Value`, 모든 `Environment`, 모든 `ArrayObject`, 모든 `StructInstanceObject`가 객체 이동 후 업데이트되어야 함. 필요한 것:
- **핸들 간접 참조**: `Value`가 안정적인 핸들 테이블의 인덱스를 저장
- 또는 **보수적 포인터 스캔** (오탐 위험)

`Value` 구조체 정의에 `// Phase 7 TODO` 주석으로 충분. 실제 구조 변경 없음.

### 이동 가능 vs 불가능 분류 전략

**단계 1 (현재)**: 모든 객체 `is_movable() = false`. 컴팩션 없음. 인프라 존재하나 비활성.

**단계 2 (선택적 활성화)**: 내부 포인터 없는 타입에 `is_movable() = true`:
- `StringObject`, `ArrayObject`, `StructInstanceObject`, `EnumInstanceObject` → true 후보
- `Environment`, `CoroutineObject`, `NativeFunctionObject`, `ScriptFunctionObject` → false

**단계 3 (전체 커버리지)**: `Environment`, `CoroutineObject` 처리. 핸들 간접 참조 `Value` 또는 완전한 update-pointers 프로토콜 필요.
