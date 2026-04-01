# Project Zephyr — Implementation Log

**Core direction: "Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

---

## 2026-04-01

### VM 최적화 — 슈퍼인스트럭션 & 인라인 캐시

#### R_BUILD_STRUCT 인라인 캐시 + StructTypeObject Shape 캐시

`array_object_churn` 벤치마크의 진짜 병목이 GC가 아닌 `R_BUILD_STRUCT` opcode의 매 호출마다 수행하는 문자열 기반 타입 탐색임을 확인하고, 두 가지 캐시를 도입했습니다.

- **IC fast-path**: `ic_shape`(StructTypeObject*)와 `ic_slot==1`(필드 순서 일치 플래그)를 활용해 두 번째 호출부터 `parse_type_name`, `expect_struct_type`, `field_slot`, `enforce_type`, `validate_handle_store`, 임시 벡터 할당을 전부 우회
- **StructTypeObject::cached_shape**: `mutable Shape* cached_shape = nullptr` 추가; `initialize_struct_instance()`가 `shape_for_struct_type()`을 매번 호출하던 것을 첫 번째 인스턴스 생성 시 1회로 줄임
- **단일 패스 필드 초기화**: IC warm-path에서 `assign(N, nil)` + 덮어쓰기 패턴 대신 `reserve(N)` + `push_back` × N으로 nil 쓰기 N번 제거

결과: array_object_churn **2,330 µs → 1,050 µs (−56%)**, Lua 5.5(1,909 µs) 대비 **약 2배 빠름**.

#### R_SI_LOOP_STEP 슈퍼인스트럭션

`R_SI_MODI_ADD_STORE` + `R_SI_ADDI_CMPI_LT_JUMP`를 단일 opcode로 융합합니다.

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

인코딩: `{dst=accum, src1=step(int8), src2=iter, operand_a=div}` + `ic_slot = (int16_limit << 16) | uint16_body_start`

융합 조건: `dst == src1`(자기 누적), 동일 루프 카운터 레지스터, `body_start ≤ 0xFFFF`, step ∈ [-128,127], limit ∈ [-32768,32767]

결과: hot_arithmetic **iter당 6 ops → 1 op**, **2,170 µs → 420 µs**, Lua 5.5(394 µs) 대비 **1.07×**.

#### UB 수정 (Signed left-shift)

Copilot 리뷰에서 지적된 4개 위치의 부호 있는 값 좌시프트(UB) 수정:
- `try_pack_r_addi_operand`, `try_pack_r_modi_operand`, `try_pack_r_si_acj`, `try_pack_r_si_cmpi_jump_false_operand`
- 부호 있는 int8/int16 값을 `uint32_t`로 캐스트 후 시프트, 최종 결과를 `static_cast<int>`로 변환

#### 벤치마크 추이

| 단계 | hot_arithmetic | array_object_churn |
|---|---|---|
| 2026-03-30 기준 | 1,130 µs | 4,310 µs |
| +R_SI_ADDI_CMPI_LT_JUMP | 535 µs | — |
| +R_BUILD_STRUCT IC | — | 2,330 µs |
| +R_BUILD_STRUCT IC + Shape 캐시 | — | 1,050 µs |
| +R_SI_LOOP_STEP | 420 µs | — |
| **최종 (Lua 5.5 기준)** | **1.07×** | **0.55×** |

---

## 2026-03-30 (오늘)

### CMake 마이그레이션 완성
- `zephyr_bench` 타겟 추가 (`ZEPHYR_BUILD_BENCH`)
- `zephyr_engine_sample` 타겟 추가 (`ZEPHYR_BUILD_SAMPLES`)
- `dap_server.cpp` 누락 수정
- `/utf-8 /bigobj /permissive-` 플래그 통일

### LSP v0.2.0 고도화
- `textDocument/signatureHelp` — 함수 파라미터 힌트
- `textDocument/rename` — 전체 문서 일괄 이름 변경
- `workspace/symbol` — 열린 문서 심볼 검색
- hover 타입추론 개선 — `let x = 42` → `int` 자동 감지
- `renameProvider`, `workspaceSymbolProvider`, `signatureHelpProvider` capability 등록
- 서버 버전 0.1.0 → 0.2.0

### .inl → .cpp 전환
- `zephyr_lexer.inl` → `zephyr_lexer.hpp` (`#pragma once` + 파일 설명 주석)
- `zephyr_types.inl` → `zephyr_types.hpp` (동일)
- `zephyr_compiler.inl` → `zephyr_compiler.hpp` (동일)
- `zephyr_parser.inl` → `zephyr_parser.cpp` (독립 TU, namespace zephyr 추가)
- `zephyr_gc.inl` → `zephyr_gc_impl.cpp` (독립 TU, namespace zephyr 추가)
- `Runtime::parse_source()` zephyr_parser.cpp으로 이동 (Parser 클래스 의존성)
- ZephyrRuntime.vcxproj + CMakeLists.txt 업데이트

### 빌드 경고 제거
- C4819 (코드페이지) → `/utf-8` 전체 vcxproj + CMakeLists.txt에 추가
- C4458 (name shadowing) → `zephyr_gc_impl.cpp:229,232` structured binding 변수명 `method_name`으로 변경
- **결과: 경고 0개**

### 정리
- `zephyr_gc.cpp` 삭제 (빈 wrapper)
- `docs/copilot_scripts/` 삭제 (Wave A~T 완료된 구현 스크립트 58개)
- `docs/CodebaseReport.md`, gc 분석 문서 3개 삭제

---

## 2026-03-28

### Register VM 완성 (master 브랜치)
- `R_*` opcodes + `R_SI_*` superinstruction
- RegisterAllocator + live range 분석, copy propagation
- `execute_register_bytecode()` + `execute_register_bytecode_coro()`
- coroutine 레지스터 통합 (`pending_call_dst_reg`, 가드 제거)
- `compact_suspended_coroutine()` 최적화
- **벤치마크**: hot_arithmetic 2.17ms (~5.4 ns/op), coroutine 635 ns/resume

### Register Spill Fallback
- `R_SPILL_LOAD` / `R_SPILL_STORE` 옵코드
- slot >= 256 시 heap spill 자동 emit
- `/bigobj` 빌드 플래그, 포맷 버전 1→2

---

## 2026-03-24

### Wave K~T 완료
- K: generic type parameters
- L: Result<T> + `?` operator
- M: module system (named imports, re-exports)
- N: lowercase primitive types
- O: 2-pass semacheck
- P: string interning + where clauses
- Q: std/json + std/collections
- R: associated functions
- S: error messages + iterator protocol
- T: std/io, std/gc, std/profiler + --profile

### Wave F~H 완료
- F: Superinstruction fusion, GC trace JSON, module caching, PGO
- G: 진단 메시지, 테스트 분리, corpus
- H: intern_string, std/math/string/collections, package.toml, CMake 초안
- **최종 벤치마크**: 5/5 PASS, host_handle 660 ns/resolve, coroutine 878 ns/resume

---

## Benchmark History

| Date | hot_arithmetic | array_churn | coroutine/resume | host/resolve | Gates |
|---|---|---|---|---|---|
| v1 baseline | 1,000 ms | — | 74,813 ns | 33,333 ns | — |
| Wave D | 3.91 ms | — | 878 ns | 660 ns | 5/5 |
| Register VM | 2.17 ms | — | 635 ns | 641 ns | 5/5 |
| 2026-03-30 | 1.13 ms | 4,310 µs | 593 ns | 641 ns | 5/5 |
| 2026-04-01 | **~420 µs** | **~1,050 µs** | ~220 µs | ~224 µs | 5/5 |
| Lua 5.5 (ref) | 394 µs | 1,909 µs | 923 µs | 303 µs | — |
