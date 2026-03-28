# Project Zephyr — 구현 진행 기록

> 수정/구현 완료 시마다 해당 항목을 즉시 업데이트한다.

**핵심 방향성: "Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

| 축 | Lua 5.4 | Zephyr 현재 | 목표 |
|----|---------|------------|------|
| arithmetic | ~3~5 ns/op | ~9.8 ns/op | ~4~5 ns/op (register VM) |
| coroutine resume | ~200~400 ns | 878 ns | ~300~400 ns |
| GC 관찰성 | 거의 없음 | ✅ 완비 | 유지 |
| 현대 문법 | 없음 | ✅ 완비 | 유지 |

---

## Wave A — 성능 최적화 기초

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 1.1 | Flat Closure / Lightweight Call | ✅ 완료 | BytecodeFunction 플래그, lightweight_args |
| 1.2 | Constant Folding + Peephole | ✅ 완료 | optimize_bytecode() 후처리 패스 |
| 1.3 | MSVC `__assume(0)` | ✅ 완료 | execute_bytecode_chunk, coroutine 루프 |
| 3.1 | `suspended_coroutines_` → `unordered_set` | ✅ 완료 | insert/erase, zephyr_gc.inl:2736 |
| 3.2 | Write Barrier 패스트패스 | ✅ 완료 | is_old_object() 조기 반환, zephyr_gc.inl:5345 |
| 4.1 | 정수 산술 인라인 패스트패스 (Add/Sub/Mul) | ✅ 완료 | int 분기 인라인, zephyr_gc.inl:2212 |
| — | 벤치 기준선 저장 (wave_a_baseline.json) | ✅ 완료 | bench/results/wave_a_baseline.json, 게이트 5/5 통과 |

---

## Wave B — 구조 안정화

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 2.1 | 파일 분할 (lexer/parser/types/compiler/gc/vm/api) | ✅ 완료 | .inl 분리 구조 이미 충족 (zephyr_lexer/parser/types/compiler/gc.inl) |
| 2.2 | 중복 코드 통합 (env chain walk) | ✅ 완료 | walk_environment_chain 템플릿 추가, lookup_binding / module_or_root_environment / ensure_capture_cells 리팩터 |
| — | stats CLI에 barrier_hits / lightweight_calls 비율 출력 | ✅ 완료 | cli/main.cpp gc.barrier_hits + vm.lightweight_calls + lightweight_call_ratio 추가 |
| — | 벤치 기준선 저장 (wave_b_baseline.json) | ✅ 완료 | bench/results/wave_b_baseline.json, 게이트 5/5 통과 (v1 대비 hot_arith 99.38%) |

---

## Wave C — GC 고도화

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 3.3 | Bitmap Card Table | ✅ 완료 | uint8_t→uint64_t 비트맵, for_each_dirty_card(countr_zero), count_remembered_cards popcount |
| 3.4 | Lazy Sweep | ✅ 완료 | budget-based gc_step() 이미 구현돼 있음 (기존 코드 확인) |
| 3.5 | Adaptive Nursery | ✅ 완료 | survival >50%→nursery 2배(최대 1MB), <10%→절반(최소 64KB), config_.gc.adaptive_nursery 플래그 |
| 4.3 | Stack Reserve | ✅ 완료 | stack.reserve(64)→reserve(local_count*2+32) |
| — | String Concat 최적화 | ✅ 완료 | apply_binary_op: StringObject fastpath, reserve+append |
| — | 벤치 기준선 저장 (wave_c_baseline.json) | ✅ 완료 | bench/results/wave_c_baseline.json, 게이트 3/5 통과 (hot_arithmetic_vs_v1 3.73% < 25% 미달, allocation 게이트 baseline 누락으로 skip) |

---

## Wave D — 값 표현 고도화

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 1.4 | NaN-boxing | ✅ 완료 | `Value` 8B NaN-boxing, Int48 range 검사, helper/VM 경로 갱신 |
| 1.5 | Shape IC (Inline Cache) | ✅ 완료 | `StructInstanceObject` shape+slot 저장, `LoadMember`/`StoreMember` IC, GC/직렬화 경로 갱신 |
| 4.4 | Instruction Compression | ✅ 완료 | `CompactInstruction` hot struct + `InstructionMetadata` sidecar, `static_assert(sizeof(CompactInstruction) <= 24)` |
| D-HF | hot_arithmetic 후속 핫픽스 | ✅ 완료 | int `Modulo` fast path + lightweight local-slot mini-dispatch + per-call opcode counter 누적, `hot_arithmetic_loop` 4,615,640ns / 목표 5,155,600ns 달성, `zephyr_tests` 통과 |
| D-SI | Superinstructions | ✅ 완료 | `SIAddStoreLocal`, `SICmpJumpIfFalse`, `SILoadLocalLoadLocal`, `SILoadLocalConstAddStoreLocal`, `SILoadLocalLocalConstModulo` 추가; `hot_arithmetic_loop` 4,954,235ns / 12.39ns-op / 400,015 opcodes → 3,593,580ns / 8.98ns-original-op 상당 / 240,014 dispatched opcodes (`14.97ns`/dispatch-op), Release x64 / `zephyr_tests` / `zephyr_bench --baseline bench\\results\\wave_d_baseline.json` 검증, 4,000,000ns 목표 달성 |

---

## Wave E1 — 언어 기능 확장

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 5.1 | String Interpolation | ✅ 완료 | `f"..."` 보간식 렉서/파서/컴파일러/런타임 및 테스트 추가 |
| 5.2 | Optional Chaining | ✅ 완료 | `?.` 단락 평가, `JumpIfNilKeep` 바이트코드 및 테스트 추가 |
| 6.1 | Class Binder | ✅ 완료 | `ZephyrClassBinder<T>`, `bind<T>()`, `make_host_object(std::shared_ptr<T>)` 추가 |
| 6.2 | Profiler | ✅ 완료 | `start_profiling()` / `stop_profiling()` 및 함수별 self/inclusive 시간 집계 추가, 비활성 시 `call_value()` 프로파일 scope 완전 우회로 오버헤드 제거 |

---

## Wave E2 — 도구 및 고급 기능

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| 5.3 | Pattern Matching | ✅ 완료 | 가드 절, OR 패턴, bool/nil 리터럴 패턴 및 Release 바이트코드 경로 테스트 추가 |
| 5.4 | Traits | ✅ 완료 | `trait` / `impl` 선언, 런타임 impl 등록, struct 인스턴스 메서드 디스패치 추가 |
| 6.3 | DAP (Debug Adapter Protocol) | ✅ 완료 | `start_dap_server()` / breakpoint API 및 최소 TCP DAP 응답 루프 추가 |
| 6.4 | VM Snapshot | ✅ 완료 | `snapshot()` / `restore_snapshot()` 바이너리 페이로드와 전역 바인딩 복원 추가 |

| E2-P | Wave E2 성능 패스 | ✅ 완료 | DAP 비활성 시 dispatch bool guard, instruction metadata/span lazy hot-path 접근, coroutine GC stress guard, trait fallback 축소, host-handle resolve `string_view` fast path 적용 후 Quick-Win에서 성공 경로 `shared_ptr` churn/중복 host method lookup 제거, 재사용 public-arg buffer + direct host property getter/setter fast path 추가, allocation gate baseline을 `v1_baseline.json`으로 고정; Release x64 / `zephyr_tests` / `zephyr_bench --baseline bench\\results\\wave_d_baseline.json` 검증 |
| E2-CF | Coroutine 회귀 수정 | ✅ 완료 | Superinstruction 도입 후 코루틴 dispatch 루프에 잔존한 오버헤드 제거 (per-dispatch 불필요 bookkeeping, coroutine GC safepoint 중복 체크), `coroutine_resume_cost` 1,899→878 ns/resume (36.3% 개선, 역대 최고); 게이트 5/5 PASS |

---

## 최종 벤치마크 (2026-03-24 안정 수치)

| 항목 | v1 기준 | 현재 | v1 대비 개선 |
|------|---------|------|-------------|
| hot_arithmetic_loop | 1,000ms | 3.91ms | 99.6% |
| host_handle ns/resolve | 33,333 ns | 660 ns | 98.0% |
| coroutine ns/resume | 74,813 ns | 878 ns | 98.8% |
| allocation full_gc_cycles | 10 cycles | 0 cycles | 100% |
| AST fallback | — | 0 | — |
| **게이트** | — | **5/5 PASS** | — |

---

## Wave F — 관찰 가능성 & 성능 심화

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| F.1 | PGO (Profile-Guided Optimization) | ✅ 완료 | /GL+/LTCG 설정 + run_pgo_build.ps1 작성 |
| F.2 | 모듈 바이트코드 캐싱 | ✅ 완료 | enable_bytecode_cache(), 파일 mtime 기반 무효화 |
| F.3 | Superinstruction 계측 | ✅ 완료 | fusion 카운터, 벤치 hit rate 포함 |
| F.4 | GC Pause Time 계측 | ✅ 완료 | p50/p95/p99, frame_budget_miss 카운터 |
| F.5 | GC 이벤트 스트림 Export | ✅ 완료 | start/stop_gc_trace(), get_gc_trace_json() |
| F.6 | Coroutine Flame/Trace | ✅ 완료 | Created/Resumed/Yielded/Completed/Destroyed 이벤트 |

---

## Wave G — 개발자 경험

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| G.1 | 테스트 체계 세분화 | ✅ 완료 | test_lexer/compiler/vm/gc/host/perf/corpus.cpp 분리 |
| G.2 | check 단계 강화 | ✅ 완료 | trait 불일치 진단, 시그니처 오류, import/export 검사 |
| G.3 | 오류 메시지 개선 | ✅ 완료 | 스택 트레이스, exhaustiveness 힌트, trait method 안내 |
| G.4 | Bytecode Dump 연동 | ✅ 완료 | superinstruction 표시, dump 회귀 테스트 |
| G.5 | Corpus 기반 회귀 테스트 | ✅ 완료 | tests/corpus/*.zph 6개 스크립트 추가 |

---

## Wave H — 플랫폼 & 확장

| # | 항목 | 상태 | 비고 |
|---|------|------|------|
| H.0 | Register-VM 최적화 + 코루틴 통합 | ✅ 완료 | `R_SI_*` superinstruction, `/GL+/LTCG` (/WPO), 코루틴 모듈 가드 제거 → 헬퍼 함수 레지스터 모드 컴파일, `pending_call_dst_reg` 로 R_CALL 중단/재개 수정. hot_arithmetic: 2.61ms, coroutine: 386µs, gates 5/5 PASS |
| H.1 | CMake 크로스플랫폼 빌드 | 🔲 미완료 | Windows + Linux 검증 |
| H.2 | .inl → .cpp 완전 분리 | ✅ 완료 | `zephyr_gc.cpp` 분리 + 내부 공용 헤더 정리 |
| H.3 | 표준 라이브러리 기초 | ✅ 완료 | `std/math.zph`, `std/string.zph`, `std/collections.zph` |
| H.4 | 패키지 모델 기초 | ✅ 완료 | `package.toml`, `set_package_root()`, 검색 경로 API |
| H.5 | 문자열 인터닝 | ✅ 완료 | `intern_string()`, GC root, 포인터 비교 최적화 |

---

_마지막 업데이트: 2026-03-28 — Wave H.0 코루틴 레지스터 통합 완료. 최종 수치는 `bench/results/wave_h0_final.json` 참조._
