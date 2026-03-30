# 향후 발전 방향 (Future Direction)

## 핵심 철학 (Core Philosophy)

**"Lua의 경량성 + 현대 문법 디자인 + 관찰 가능한 GC/코루틴 런타임"**

오직 게임 엔진 임베딩에 최적화된 안전하고 예측 가능한 스크립팅 레이어를 제공하는 것이 목표입니다.

## 성능 목표 (Performance Targets)

Zephyr의 실시간 성능이 어느 정도 경쟁력을 갖추는지 확인하기 위하여, 지속적으로 **`lua_baseline`** (Lua 5.4의 평균 성능 기준선)을 기준으로 측정을 진행합니다.

| 메트릭 지표 | `lua_baseline` 기준 | 현재 Zephyr 수치 | 최종 목표치 |
|---|---|---|---|
| 단순 연산 (ns/op) | 3~5 | ~8 | 4~5 |
| 코루틴 재개 (resume) | 200~400 ns | 593 ns | 300~400 ns |
| 호스트 핸들 읽기 (resolve) | — | 641 ns | < 300 ns |

## 개발 로드맵 (Roadmap)

### 단기 목표 (Short-term)

| # | 작업 요건 | 비고 |
|---|---|---|
| 1 | AOT 컴파일 도입 | 바이트코드 → 네이티브 바이너리 (LLVM 또는 QBE 백엔드) |
| 2 | JIT 머신 (핫패스) | 반복 횟수(Opcodes)에 따른 런타임 지연 컴파일 (ORC JIT) |
| 3 | LSP 타입 힌트 (Inlay hints) | `textDocument/inlayHint` — 변수 선언 시 생략된 타입 렌더링 |
| 4 | 회귀 (Regression) 테스트 보강 | 파서(`zephyr_parser.cpp`) 및 `zephyr_gc_impl.cpp` 파일 분리를 통한 TU 단위 검증 |

### 중기 목표 (Mid-term)

| # | 작업 요건 | 비고 |
|---|---|---|
| 5 | 전용 패키지 매니저 | `package.toml` 네트워킹 및 lock / 배포 시스템 구축 |
| 6 | 표준 라이브러리 (std) 확장 | `std/net`, `std/async`, `std/test` 탑재 |
| 7 | REPL 환경 개선 | 멀티라인 입력 쉘, 히스토리(방향키), 탭(Tab) 자동완성 |

### 장기 목표 (Long-term)

| # | 작업 요건 | 비고 |
|---|---|---|
| 8 | 셀프 호스팅 컴파일러 | Zephyr 언어 그 자체로 개발하는 Zephyr 컴파일러 파서 작성 |
| 9 | WASM 타겟 환경 | WebAssembly 환경(Emscripten / wasm32-unknown-unknown)으로 브라우저 구동 |

## AOT/JIT 백엔드 프레임워크 비교

| 백엔드 후보 | JIT | AOT | C++ 통신 구조 | 비고 |
|---|---|---|---|---|
| LLVM | ✅ (ORC) | ✅ | C++ 네이티브 지원 | 컴파일 무거움 (약 50MB 용량 차지) |
| Cranelift | ✅ | ✅ | Rust FFI 브릿지 작성 필요 | 빠르고 가벼움 (Rust 툴체인 의존성) |
| QBE | ❌ | ✅ | C 라이브러리 | 극단적으로 가벼운(~200KB) 배포 용량 |
| MIR/libmir | ✅ | ❌ | C API 연동 | Red Hat 스폰서, JIT 전용 |

> **결론**: JIT가 반드시 있어야만 실무 프로젝트에 쓸 수 있는 것은 아닙니다. 실제로 바이트코드에 불과했던 여러 엔진 스크립터 언어들의 성능은 대부분 최적화 패스(슈퍼인스트럭션 등)만으로 극적인 효율을 입증한 전례가 많습니다. 따라서 위 성능 비교표(`lua_baseline`)를 상시 모니터링하다 구조적인 한계 시점에 다다랐을 때만 도입을 고려합니다.

## 개발 제외 대상 (Non-Goals)

다음 요소들은 프로젝트 목적 및 철학에 위배되므로 도입하지 않을 계획입니다:
- 복잡한 빌림 검사기(Borrow checking) 혹은 라이프타임(Lifetime) 직접 명시
- 네이티브 스택과 혼용되어 복잡도를 높이는 `async/await` 패러다임 (모두 Zephyr 코루틴으로 완전 대체)
- 단순 런타임 리플렉션(Reflection) 및 동적 디스패치 이외의 과도한 메타프로그래밍(Metaprogramming) 매크로
