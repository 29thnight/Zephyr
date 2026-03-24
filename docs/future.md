# Zephyr future 방향 제안

## 1. 문서 목적 및 방향성

### 핵심 방향성

> **"Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

Zephyr는 Lua가 갖지 못한 두 가지를 이미 달성했다.
- **현대 문법**: f-string, optional chaining, pattern matching, traits, 제네릭 방향
- **관찰 가능한 런타임**: generational GC, adaptive nursery, DAP 서버, profiler, VM snapshot

남은 과제는 **Lua 수준의 경량성** 달성이다.

| 축 | Lua 5.4 | Zephyr 현재 | 목표 |
|----|---------|------------|------|
| arithmetic | ~3~5 ns/op | ~9.8 ns/op | ~4~5 ns/op |
| coroutine resume | ~200~400 ns | 878 ns | ~300~400 ns |
| GC 관찰 | 거의 없음 | ✅ 완비 | 유지 |
| 현대 문법 | 없음 | ✅ 완비 | 유지 |

경량성 달성의 핵심 레버: **Register-based VM** (stack pop/push 제거, 2~3x 개선 기대).

---

이 문서는 현재 구현(`docs\current.md`, `docs\process.md`)을 바탕으로, 위 방향성을 강화하기 위한 개선 제안서다.

아래 내용은 크게 두 축으로 나뉜다.

- **추가 개선 및 최적화 방안**: 지금 코드베이스를 더 빠르고 안정적이며 관리 가능하게 만드는 단기/중기 개선
- **발전 및 확장 방안**: Zephyr를 더 넓게 쓰이게 만들기 위한 장기 확장 방향

## 2. 현재 Zephyr의 강점

현재 구현 기준으로 Zephyr는 이미 다음 강점을 가지고 있다.

- Rust 스타일 문법과 현대적 언어 표면 (f-string, optional chaining, pattern matching, traits)
- NaN-boxing Value (8B), Shape IC, CompactInstruction, Superinstruction 기반 고성능 바이트코드 VM
- AST fallback 0 — 모든 실행 경로가 네이티브 바이트코드
- 코루틴과 heap-resident frame (878 ns/resume 달성)
- 호스트 핸들 수명 정책 (`Frame`, `Tick`, `Persistent`, `Stable`)
- Generational GC + Compaction + Adaptive Nursery + Bitmap Card Table
- ZephyrClassBinder<T> 템플릿 바인딩, Profiler API, DAP 서버, VM Snapshot
- 직렬화, 콜백 캡처, runtime stats, 벤치마크 게이트 (5/5 통과)

## 3. 추가 개선 및 최적화 방안

### 3.1 바이트코드 경로 계측 강화

AST fallback은 이미 완전히 제거됐다. 남은 과제는 bytecode 실행의 **관찰 가능성** 강화다.

- `dump-bytecode` 결과와 테스트를 연결해 회귀를 자동 감지
- 언어 기능별 "bytecode-only coverage" 리포트 추가
- superinstruction hit rate를 벤치마크 리포트에 포함
- 디버그/릴리스 간 실행 경로 차이를 계측해 재현성 검증

### 3.2 Dispatch 추가 최적화

Superinstruction은 구현됐다. 다음 단계:

- 벤치마크로 실제 fusion 비율 계측 및 새 패턴 발굴 (현재 hot_loop에서 240K/400K 감소 확인)
- PGO(Profile-Guided Optimization) 빌드 — MSVC `/GL` + `/LTCG` + PGO 프로파일 수집으로 switch dispatch 분기 예측 강화
- 모듈 바이트코드 캐싱 — 반복 import 시 파싱/컴파일 스킵 (현재 module_import 744μs → 수십 μs 목표)
- Register-based VM 검토 — 스택 팝/푸시 제거로 이론상 추가 2~3x 개선 가능 (대규모 리팩터, 장기)

### 3.3 GC pause time과 locality 추가 개선

Adaptive Nursery, Bitmap Card, Lazy Sweep은 완료됐다. 남은 과제:

- 프레임 예산 기반 GC 정책 세분화 (현재 budget-based step 존재, 더 정교한 예측 제어)
- old generation compaction 조건과 비용 계측 정교화
- coroutine/frame/environment 전용 arena 실제 활성화로 locality 개선
- runtime stats에 pause time percentile과 frame budget miss 횟수 추가

Zephyr의 차별점은 "GC가 있음"이 아니라 "게임 루프 안에서도 통제 가능함"이 되어야 한다.

### 3.4 코루틴 스케줄링 친화성 강화

코루틴 비용은 878 ns/resume로 대폭 개선됐다. 다음 단계:

- host-driven scheduler 예제와 정책 API 추가
- cancellation, timeout, parent-child coroutine 관리 모델 정리
- coroutine flame/trace 형태의 관찰성 추가 (현재 DAP + stats만 존재)
- 코루틴 생성/소멸/중단 원인별 통계 강화

### 3.5 호스트 호출 경계 추가 최적화

ZephyrClassBinder<T>와 lightweight call이 완료됐다. 남은 과제:

- 바인더 API에 "읽기 전용", "noexcept", "pure" 힌트 추가해 최적화 여지 확보
- 자주 쓰는 HostObject 경로의 boxing/unboxing 비용 추가 절감
- 스크립트 ↔ 호스트 간 소형 값 타입 최적화

### 3.6 타입 시스템과 오류 메시지 개선

동적 런타임 위에 pattern matching, traits가 추가됐다. 보강 과제:

- 타입 오류 메시지에 더 풍부한 문맥 제공
- 함수 시그니처, struct/trait 구현 불일치 진단 강화
- `check` 단계에서 더 많은 정적 검증 수행
- import/export 경계의 타입 일관성 검사
- trait 구현 누락, optional chaining nil 전파 관련 오류 진단 구체화

### 3.7 툴링 포맷 체계화

DAP 서버, Profiler, VM Snapshot, dump-bytecode가 완료됐다. 다음 단계:

- AST/bytecode/source span 연결 정보 정교화 (현재 span_line만 존재)
- 프로파일 리포트를 외부 시각화 도구가 읽기 좋은 JSON으로 안정화
- GC 이벤트 스트림 export 지원 (현재 stats만 있고 event stream 없음)
- benchmark report schema versioning

### 3.8 테스트 체계 세분화

- 기능군별 테스트 파일 분리 (현재 tests.cpp 단일 파일 ~2,300줄)
- parser / compiler / VM / GC / serialization / host integration / perf test 계층화
- fuzzing 또는 property-based test 도입 검토
- 스크립트 corpus 기반 회귀 테스트 추가
- superinstruction hit rate 회귀 감지 테스트

## 4. 발전 및 확장 방안

### 4.1 언어 완성도 확장

pattern matching (guard, OR, literal), traits/impl은 완료됐다. 남은 방향:

- 제네릭 / parametric polymorphism
- 더 정교한 trait 제약 (where 절, 다중 trait 바운드)
- 패턴 매칭 추가 강화 (중첩 destructuring, array/record 패턴)
- 모듈 가시성/패키지 경계 모델
- 에러 처리 구문 (`result`, `try`, `defer` 류) 정리
- 문자열 인터닝 / 중복 제거 (현재 StringObject마다 별도 할당)

### 4.2 표준 라이브러리 확장

Zephyr가 실제 프로젝트에 쓰이려면 기본 라이브러리가 중요하다.

우선순위가 높은 후보:
- 수학, 난수, 시간, 경로, JSON 유틸리티
- 문자열/컬렉션 유틸리티 확장
- 이벤트/신호 패턴용 헬퍼
- 코루틴 친화 async 패턴 API
- 디버그/로그/트레이스 유틸리티

### 4.3 패키지 및 모듈 배포 모델

- 패키지 레이아웃 규약
- 모듈 검색 경로와 버전 전략
- lockfile 또는 package manifest 설계
- 내장 표준 모듈과 외부 모듈의 경계 정의

### 4.3.5 Register-based VM 전환 `[XL]` `[Risk: High]` ⭐ 경량성 핵심

**Lua 수준 경량성 달성의 가장 중요한 단계.**

현재 stack-based VM은 매 opcode마다 push/pop 발생. register-based 전환 시:
- operand를 로컬 슬롯 인덱스로 직접 참조 → 스택 조작 제거
- `CompactInstruction`에 dst/src1/src2 레지스터 인덱스 추가
- VM dispatch: `locals[dst] = locals[src1] OP locals[src2]`
- 컴파일러: 레지스터 할당 (선형 스캔)
- 기대 효과: ~4~5 ns/op → Lua 5.4 수준, 코루틴 resume ~300~400 ns 목표
- 전체 컴파일러/VM/coroutine 재설계 필요 → 별도 브랜치 권장

### 4.4 멀티플랫폼과 빌드 체계 확장

현재 Visual Studio / Windows 전용. 장기 과제:

- CMake 크로스플랫폼 빌드 진입점
- Windows 외 환경 빌드/테스트 검증
- 정적 라이브러리 외 shared library 옵션
- 콘솔/모바일 환경 제한 모드 구성
- .inl 구조 → .cpp 완전 분리 (현재 단일 컴파일 유닛, 빌드 속도 문제)

### 4.5 에디터와 개발자 경험

DAP 서버, Profiler가 있으므로 연결하면 현실적:

- LSP 서버 (문법 하이라이트, 진단, 자동완성, jump-to-definition)
- bytecode/GC/profiler 정보를 연결한 개발자 HUD
- REPL 개선과 playground 제공

### 4.6 엔진 통합 제품화

- 엔진 오브젝트 바인딩 패턴 표준화 (ZephyrClassBinder 기반)
- 씬/프레임/틱 생명주기와 핸들 정책 문서화
- 이벤트, AI, UI, 컷신 등 대표 도메인 샘플 제공
- "엔진 팀이 붙여 쓰기 쉬운" API 안정성 확보

### 4.7 AOT/하이브리드 실행 모델

장기 검토 과제:

- hot path 전용 ahead-of-time lowering
- script bundle precompilation (모듈 캐싱과 연계)
- shipping build용 strip/debug info 분리
- serialization-safe precompiled module 포맷

## 5. 추천 우선순위

### 1순위: 관찰 가능성 & 안정성 강화

- superinstruction hit rate 계측 및 신규 패턴 발굴
- PGO 빌드 적용 (즉시 효과 기대)
- 모듈 바이트코드 캐싱
- GC pause time percentile 계측
- 오류 메시지 품질 향상

### 2순위: 개발할 때 편한 언어

- `check` 단계 강화 (trait 구현 불일치 등)
- 테스트 체계 정비 (기능군별 분리)
- 에디터/LSP 기초 투자
- corpus 기반 회귀 테스트

### 3순위: 제품처럼 쓰는 플랫폼

- 표준 라이브러리 확장
- 모듈/패키지 모델
- 멀티플랫폼 빌드 (CMake)
- 엔진 통합 가이드와 샘플

### 4순위: 장기 차별화

- 더 강한 타입 시스템 (제네릭, trait 바운드)
- Register-based VM
- AOT/hybrid 실행 모델

## 6. 방향성 요약

### 핵심 포지셔닝

> **"Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

Zephyr는 Lua를 단순히 따라가는 것이 아니라, Lua가 포기한 두 가지(현대 문법, 런타임 관찰성)를 취하면서 경량성을 좁혀가는 방향이다.

### 성능 로드맵

```
현재:  ~9.8 ns/op (stack-based + superinstruction)
Wave F: ~8 ns/op (PGO + 모듈 캐싱)
Wave H: ~4~5 ns/op (register-based VM 전환 시 Lua 5.4 수준)
```

### Zephyr가 답해야 할 질문

- Lua만큼 가볍게 임베딩할 수 있는가?
- 게임 루프 안에서 예측 가능하게 돌 수 있는가?
- 엔진 객체를 안전하고 편리하게 바인딩할 수 있는가?
- 문제가 생겼을 때 원인을 빠르게 볼 수 있는가?
- 스크립터가 현대적인 문법으로 빠르게 작성할 수 있는가?
