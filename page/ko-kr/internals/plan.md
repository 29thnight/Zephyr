# 프로젝트 Zephyr — 구현 달성 계획표 (Implementation Plan)

## 달성 완료 페이즈 (Completed Waves)

| 페이즈 | 주요 구현 내용 | 상태 |
|---|---|---|
| A | Flat Closure, 동적 상수 접기, Write Barrier, 정수 타입 패스트패스 | ✅ |
| B | 모듈 파일 분할 프레임워크 설계 및 stats CLI 제작 | ✅ |
| C | Bitmap Card Table 알고리즘, 지연 Sweep, 동적 Nursery 크기 조절, 문자열 결합 | ✅ |
| D | NaN-boxing 값 체계, Shape IC 최적화, 바이트코드 인스트럭션 압축 | ✅ |
| E1 | `f"..."` 보간, `?.` 옵셔널 체이닝, ZephyrClassBinder 래퍼, Profiler API | ✅ |
| E2 | 확장 패턴 매칭 처리, Traits/impl 구현, DAP 디버깅 어댑터 준비, VM 스냅샷 | ✅ |
| F | 슈퍼인스트럭션(SI) 융합 패스, GC 동작 추적 시스템, 모듈 바이트코드 `.zphc` 캐시, PGO 지원 | ✅ |
| G | 진단 에러 메시지(Diagnostics) 개선, 스택 트레이스 추적, 단위 Corpus 테스트 체계 정비 | ✅ |
| H | String interning 구현, `std/math`, `std/string`, `std/collections` 작성, Cmake/package.toml | ✅ |
| H.0 | 순수 레지스터 기반 VM 아키텍처 완전 이식 (`R_*` 명령어셋, 레지스터 할당기 제작, Coroutine 통합 재녹화) | ✅ |
| K | 함수 및 구조체 대상 `Generic` 타입 파라미터 매핑 지원 | ✅ |
| L | `Result<T>` 오류 통합, `?` 에러 전파 생략 연산자, 패턴 구조 확장 | ✅ |
| M | Named Import 지정, 모듈 Re-export 를 포함한 `ZephyrModule` 로더 시스템 | ✅ |
| N | 원시 타입 소문자 키워드 컨벤션 획일화 (`int`/`float`/`bool`/`string`/`void`/`any`) | ✅ |
| O | 2단계 투패스 의미 분석(Semacheck) 보강 (선언 우선 Hoisting, 트레이트 impl 완전성 증명) | ✅ |
| P | 트레이트 제약문(`where T: Trait`) 도입 | ✅ |
| Q | `std/json` 통신 모듈 및 `std/collections` 고도화 | ✅ |
| R | 정적 연계 함수 공간 활용 (`TypeName::fn`) | ✅ |
| S | 이터레이터 프로토콜 연동(`for in`) 보강 | ✅ |
| T | `std/io`, `std/gc`, `std/profiler`, CLI단 `--profile` 스위치 활성 | ✅ |
| Spill | 256개 상회하는 초과 로컬 변수에 대비하기 위한 `R_SPILL_LOAD/STORE` 명령어 및 v2 포맷 배포 | ✅ |

## 페이즈 완료 이후 기술 부채 해결 현황 (2026-03-30 기준)

| 추가 대응 과제 | 상태 |
|---|---|
| CMake 빌드 통합 완료 (bench / sample / dap 하위 타겟 전부 포함) | ✅ |
| LSP v0.2.0 개량 (인자 시그니처 힌트, 심볼 리네임 팩토링, 자동 타입 추론 마우스 호버 등) | ✅ |
| 인라인(`.inl`) 파일 5종 → 독립적인 ODR 목적의 `.cpp` Translation Unit(TU) 전환 | ✅ |
| MSVC 내부 빌드 경고문구 전부 제거 (`/utf-8` 유니코드 플래그, 그림자 변수 `C4458` 수정) | ✅ |
| 불필요 잔재 문서 63건(copilot_scripts 외 쓰레기 폴더들) 일괄 정리 | ✅ |
| 빈 파일로 남아있던 껍데기 `zephyr_gc.cpp` 제거 | ✅ |

## 향후 백로그 (Next Tasks)

| # | 예정 작업 요건 | 중요도 |
|---|---|---|
| 1 | AOT 선행 컴파일 체제 도입 (LLVM / QBE 백엔드 컴파일러) | 중간 |
| 2 | LSP 에디터 인레이 힌트 (Inlay hints - 변수 옆 가상 타입 표기) | 낮음 |
| 3 | 외부 패키지 매니저 구축 (Network fetch 및 lock 파일 체계) | 중간 |
| 4 | 네트워킹 및 비동기 시스템 `std/net`, `std/async` 라이브러리 지원 | 낮음 |
| 5 | REPL 개발 쉘 환경 편의성 대폭 고도화 (방향키 역사 조회 등) | 낮음 |
