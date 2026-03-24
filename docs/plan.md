# Project Zephyr — 구현 계획

## Context

Project Zephyr는 C++20 게임 스크립팅 VM.

**핵심 방향성: "Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

| 축 | Lua 5.4 | Zephyr 현재 | 목표 |
|----|---------|------------|------|
| arithmetic | ~3~5 ns/op | ~9.8 ns/op | ~4~5 ns/op (register VM) |
| coroutine resume | ~200~400 ns | 878 ns | ~300~400 ns |
| GC 관찰성 | 거의 없음 | ✅ 완비 | 유지 |
| 현대 문법 | 없음 | ✅ 완비 | 유지 |

Wave A~E2 전체 완료 (2026-03-24). 현재 벤치마크: 5/5 게이트 통과.

핵심 파일:
- `src/zephyr.cpp` — 모든 구현 (includes .inl files)
- `src/zephyr_*.inl` — lexer / parser / types / compiler / gc
- `tests/tests.cpp` — 테스트
- `include/zephyr/api.hpp` — 공개 API
- `bench/results/wave_d_baseline.json` — 현재 벤치마크 기준선
- `docs/process.md` — 구현 진행 기록

---

## ✅ WAVE A — 성능 최적화 기초 (완료)

- 1.1 Flat Closure / Lightweight Call
- 1.2 Constant Folding + Peephole (optimize_bytecode)
- 1.3 MSVC `__assume(0)`
- 3.1 `suspended_coroutines_` → `unordered_set`
- 3.2 Write Barrier 패스트패스
- 4.1 정수 산술 인라인 패스트패스 (Add/Sub/Mul)
- 벤치 기준선: wave_a_baseline.json (5/5 통과)

## ✅ WAVE B — 구조 안정화 (완료)

- 2.1 파일 분할 (.inl 분리 구조)
- 2.2 중복 코드 통합 (walk_environment_chain)
- stats CLI barrier_hits / lightweight_calls 출력
- 벤치 기준선: wave_b_baseline.json (5/5 통과)

## ✅ WAVE C — GC 고도화 (완료)

- 3.3 Bitmap Card Table (uint64_t, _BitScanForward64)
- 3.4 Lazy Sweep (budget-based gc_step)
- 3.5 Adaptive Nursery (survival rate 기반 크기 조정)
- 4.3 Stack Reserve (local_count*2+32)
- String Concat 최적화
- 벤치 기준선: wave_c_baseline.json

## ✅ WAVE D — 값 표현 고도화 (완료)

- 1.4 NaN-boxing (Value 16B→8B, uint64_t bits_)
- 1.5 Shape IC (StructInstanceObject Shape*+vector<Value>, IC 슬롯)
- 4.4 Instruction Compression (CompactInstruction ≤24B + InstructionMetadata sidecar)
- hot_arithmetic 패스트패스 확장 (비교 연산 int fast path, Modulo)
- Superinstruction (SIAddStoreLocal, SICmpJumpIfFalse 등, opcode 수 40% 감소)
- 벤치 기준선: wave_d_baseline.json (5/5 통과, hot_arith 47.72% 개선)

## ✅ WAVE E1 — 언어 기능 확장 (완료)

- 5.1 String Interpolation `f"..."`
- 5.2 Optional Chaining `?.` (JumpIfNilKeep 옵코드)
- 6.1 ZephyrClassBinder<T> / bind<T>() 템플릿 바인딩
- 6.2 Profiler API (start_profiling / stop_profiling / ZephyrProfileReport)

## ✅ WAVE E2 — 도구 및 고급 기능 (완료)

- 5.3 Pattern Matching 확장 (guard if, OR 패턴, 리터럴 패턴)
- 5.4 Traits / impl 블록
- 6.3 DAP 서버 (TCP, breakpoints, step, variables)
- 6.4 VM Snapshot (snapshot/restore_snapshot, 바이너리 직렬화)
- E2 성능 패스: DAP/trait/GC 핫패스 격리, host-handle string_view
- Quick-win: host_handle 회귀 수정, allocation gate v1_baseline 고정
- Coroutine 회귀 수정: 코루틴 디스패치 루프 superinstruction 오버헤드 제거

### 최종 벤치마크 (2026-03-24)
```
hot_arithmetic:   3,910,670 ns (3.91ms) — v1 대비 99.6% 개선
host_handle:        660 ns/resolve       — v1 대비 98.0% 개선
coroutine:          878 ns/resume        — v1 대비 98.8% 개선
게이트: 5/5 PASS
```

---

## 🔲 WAVE F — 관찰 가능성 & 성능 심화

**시작 조건:** Wave E2 완료 ✅

### F.1 PGO (Profile-Guided Optimization) `[S]`
- MSVC `/GL` + `/LTCG` + PGO 프로파일 수집 적용
- 벤치마크 워크로드로 프로파일 수집 후 최적화 빌드
- 기대 효과: switch dispatch 분기 예측 개선, 추가 5~15% 성능 향상

### F.2 모듈 바이트코드 캐싱 `[M]`
- 반복 import 시 파싱/컴파일 스킵, 직렬화된 BytecodeFunction 재사용
- 캐시 키: 파일 경로 + 수정 시각 (또는 해시)
- 기대 효과: module_import 744μs → 수십 μs

### F.3 Superinstruction 계측 `[S]`
- optimize_bytecode()에서 fusion 횟수 카운터 추가
- 벤치마크 리포트에 superinstruction hit rate 포함
- 신규 패턴 발굴 (LoadLocal+LoadLocal+Add, LoadConst+Add+StoreLocal 등)

### F.4 GC Pause Time 계측 `[S]`
- GC 이벤트 타임스탬프 기록 (young/full collection 시작~종료)
- stats에 pause time percentile (p50/p95/p99) 출력
- frame budget miss 횟수 카운터 추가

### F.5 GC 이벤트 스트림 Export `[M]`
- GC 이벤트를 JSON 스트림으로 export (외부 시각화 도구용)
- ZephyrVM::start_gc_trace() / stop_gc_trace() API

### F.6 Coroutine Flame/Trace `[M]`
- 코루틴 생성/yield/resume/소멸 타임스탬프 기록
- ZephyrProfileReport에 coroutine trace 포함

### 벤치 기준선
- `bench/results/wave_f_baseline.json`
- F 완료 후 저장

---

## 🔲 WAVE G — 개발자 경험 개선

**시작 조건:** Wave F 완료 (또는 F.1~F.3 완료 후 병렬 시작 가능)

### G.1 테스트 체계 세분화 `[M]`
- tests.cpp 단일 파일 분리:
  - `tests/test_lexer.cpp`
  - `tests/test_parser.cpp`
  - `tests/test_compiler.cpp`
  - `tests/test_vm.cpp`
  - `tests/test_gc.cpp`
  - `tests/test_host.cpp`
  - `tests/test_perf.cpp`
- MSBuild .vcxproj에 새 파일 추가

### G.2 `check` 단계 강화 `[M]`
- trait 구현 불일치 정적 진단 (impl 누락 메서드)
- 함수 시그니처 불일치 오류 메시지 개선
- optional chaining nil 전파 경고
- import/export 경계 타입 일관성 검사

### G.3 오류 메시지 품질 개선 `[M]`
- 런타임 타입 오류에 스택 트레이스 + 소스 위치 포함
- pattern matching exhaustiveness 힌트
- trait method not found 시 impl 누락 안내

### G.4 Bytecode Dump 연동 `[S]`
- dump-bytecode 출력과 테스트를 연결해 회귀 자동 감지
- superinstruction 출력 포맷 추가

### G.5 Corpus 기반 회귀 테스트 `[M]`
- `tests/corpus/*.zph` 스크립트 집합 추가
- 각 스크립트에 대해 예상 출력/바이트코드 패턴 검증

---

## 🔲 WAVE H — 플랫폼 & 확장

**시작 조건:** Wave G 완료

### H.0 Register-based VM `[XL]` `[Risk: High]` ⭐ 경량성 핵심
- 현재 stack-based VM → register-based 전환 (dst/src1/src2 레지스터 인덱스)
- `CompactInstruction`에 레지스터 인덱스 필드 추가
- 컴파일러: 선형 스캔 레지스터 할당 구현
- VM dispatch: `locals[dst] = locals[src1] OP locals[src2]`
- 기대 효과: ~8 ns/op → ~4~5 ns/op (Lua 5.4 수준), coroutine ~300~400 ns
- 전체 컴파일러/VM/coroutine 재설계 필요 → 별도 브랜치 권장

### H.1 CMake 크로스플랫폼 빌드 `[L]`
- CMakeLists.txt 추가 (zephyr_core, zephyr_tests, zephyr_bench, zephyr_cli)
- Windows (MSVC) + Linux (GCC/Clang) 검증

### H.2 .inl → .cpp 완전 분리 `[L]` `[Risk: Medium]`
- 현재 단일 컴파일 유닛 (zephyr.cpp includes all .inl)
- 각 .inl을 독립 .cpp로 전환, 헤더 의존성 정리
- 기대 효과: 단일 파일 수정 시 전체 재빌드 방지

### H.3 표준 라이브러리 초기 구성 `[M]`
- `std/math.zph`, `std/string.zph`, `std/collections.zph`
- 난수, 시간, JSON 기초 유틸리티
- 코루틴 친화 async 헬퍼

### H.4 패키지 모델 기초 `[M]`
- 패키지 레이아웃 규약 정의
- 모듈 검색 경로 설정 API
- package.toml / lockfile 구조 설계

### H.5 문자열 인터닝 `[S]`
- 동일 문자열 값 → 단일 StringObject 공유
- 인터닝 테이블 (unordered_map<string, StringObject*>) GC root 등록
- 기대 효과: 반복 문자열 비교/GC 비용 절감

---

## 전체 진행 순서

```
[✅ Wave A — 성능 기초]
[✅ Wave B — 구조 안정화]
[✅ Wave C — GC 고도화]
[✅ Wave D — 값 표현 고도화 + Superinstruction]
[✅ Wave E1 — 언어 기능 확장]
[✅ Wave E2 — 도구 및 고급 기능]
        ↓
[🔲 Wave F — 관찰 가능성 & 성능 심화]
   F.1 PGO → F.2 모듈 캐싱 → F.3 SI 계측 → F.4 GC pause → F.5 GC trace → F.6 coroutine trace
        ↓
[🔲 Wave G — 개발자 경험]
   G.1 테스트 분리 → G.2 check 강화 → G.3 오류 메시지 → G.4 dump 연동 → G.5 corpus
        ↓
[🔲 Wave H — 플랫폼 & 확장]
   H.0 Register-VM ⭐ → H.1 CMake → H.2 .cpp 분리 → H.3 표준 라이브러리 → H.4 패키지 → H.5 문자열 인터닝
```

## 검증 전략

- 매 태스크 완료 시: `msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64` + `x64\Release\zephyr_tests.exe`
- Wave 경계마다: `x64\Release\zephyr_bench.exe --baseline bench/results/wave_d_baseline.json`
- 기준선 저장: `bench/results/wave_f_baseline.json` 등
