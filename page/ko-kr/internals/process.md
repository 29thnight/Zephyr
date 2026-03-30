# 구현 프로세스 로그 (Implementation Log)

**핵심 개발 방향: "Lua 수준의 임베딩 경량성 + 최신 문법 지원 + 제어 가능한 GC 및 코루틴 런타임"**

---

## 2026-03-30 (오늘)

### CMake 마이그레이션 완성
- `zephyr_bench` 벤치마크 타겟 추가 (`ZEPHYR_BUILD_BENCH`)
- `zephyr_engine_sample` 샘플 엔진 타겟 추가 (`ZEPHYR_BUILD_SAMPLES`)
- 빌드에서 누락되었던 `dap_server.cpp` 파일 포함 수정
- `/utf-8 /bigobj /permissive-` MSVC 플래그 전역 통일

### LSP v0.2.0 고도화
- `textDocument/signatureHelp` — 함수 파라미터 힌트 툴팁
- `textDocument/rename` — 전체 워크스페이스(Workspace) 일괄 변수 이름 변경
- `workspace/symbol` — 열린 문서 혹은 워크스페이스 내 전역 심볼 검색
- 마우스 호버(Hover) 시의 타입 추론 개선 — `let x = 42` → `int` 자동 감지 후 표시
- `renameProvider`, `workspaceSymbolProvider`, `signatureHelpProvider` 서버 capability 등록
- 서버 버전 0.1.0 → 0.2.0 릴리즈

### `.inl` 매크로 인라인 파일을 `.cpp` 로 구조 분리 전환
- `zephyr_lexer.inl` → `zephyr_lexer.hpp` (`#pragma once` + 파일 설명 주석 전개)
- `zephyr_types.inl` → `zephyr_types.hpp`
- `zephyr_compiler.inl` → `zephyr_compiler.hpp`
- `zephyr_parser.inl` → `zephyr_parser.cpp` (별도 목적 파일(TU)로 독립 분리 및 namespace 추가)
- `zephyr_gc.inl` → `zephyr_gc_impl.cpp` (별도 목적 파일(TU)로 독립 분리 및 namespace 추가)
- `Runtime::parse_source()` 의존성을 zephyr_parser.cpp으로 이동 조치

### 컴파일러 빌드 경고 완전 제거 (Zero Warnings)
- C4819 (코드페이지 문자 깨짐 방지) → `/utf-8` 전체 프로젝트 + CMakeLists.txt에 추가
- C4458 (섀도잉 경고) → `zephyr_gc_impl.cpp` 안의 임시 변수명 중복 회피 처리
- **결과: Visual Studio 상에서 경고 0건 달성**

### 문서 및 파일 정리 (Cleanup)
- `zephyr_gc.cpp` (단순 래퍼로 남은 빈 파일) 삭제
- `docs/copilot_scripts/` 폴더 내 Wave A~T 관련 완료된 코파일럿/LLM 대화형 스크립트 58종 일괄 삭제 유지보수
- 더이상 유효하지 않은 `docs/CodebaseReport.md` 및 구버전 분석 문서 3종 파기 처리

---

## 2026-03-28

### Register VM 아키텍처 완성 (master 브랜치 적용)
- 스택 기반 명령어를 폐기하고 `R_*` 옵코드 및 `R_SI_*` 슈퍼 인스트럭션 체계로 재작성
- RegisterAllocator 설계 + 변수 수명 주기 분할 분석, 불필요한 Copy propagation 최적화 제거 달성
- `execute_register_bytecode()` + `execute_register_bytecode_coro()` 엔진 코어 투트랙 분리
- 코루틴 레지스터 스냅샷 오버헤드 통합 단순화 처리 시행
- `compact_suspended_coroutine()` 힙 낭비 최소화 리팩토링
- **벤치마크 결과**: hot_arithmetic 루프 2.17ms (~5.4 ns/op) 돌파, coroutine 문맥 전환 재개(resume) 시간 635 ns 단축

### 가상 레지스터 스필 오버플로우 (Register Spill Fallback) 대비 설계
- `R_SPILL_LOAD` / `R_SPILL_STORE` 옵코드 파이프라인 개통
- 메모리 설계 한계 상 1개 함수 내에 선언된 로컬 변수(슬롯)가 256개 돌파 시 힙 스필 자동 emit 분기 생성
- MSVC 컴파일러 `/bigobj` 빌드 플래그 활성화, 스필 바이너리 덤프 포맷 버전 v1→v2 갱신

---

## 성능 벤치마크 역사 (Benchmark History)

초기 성능 측정의 비교 지표는 Lua 5.4 기반으로 측정한 **`lua_baseline`** 환경을 기준으로 표기합니다.

| 일자 (Date) | 연산 루프 (hot_arithmetic) | 코루틴 재개 지연 (resume) | 호스트 핸들 조회 비용 (resolve) | 통과 여부 |
|---|---|---|---|---|
| `lua_baseline` 기준 | 1,000 ms (느림) | 74,813 ns | 33,333 ns | — |
| Wave D 투입 시 | 3.91 ms | 878 ns | 660 ns | 5/5 |
| Register VM 이식 시 | 2.17 ms | 635 ns | 641 ns | 5/5 |
| 2026-03-30 (최신) | 1.13 ms | 593 ns | 641 ns | 5/5 |
