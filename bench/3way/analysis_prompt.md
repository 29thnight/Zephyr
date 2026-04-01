# Zephyr VM 3-Way Benchmark Analysis Request

## Context

Zephyr는 게임 엔진용 임베디드 스크립트 언어입니다. 동급 임베디드 언어인 Lua 5.5와 Gravity와의 성능 비교 벤치마크를 수행했습니다. 동일한 알고리즘을 세 언어로 구현하고, 각 언어의 CLI로 실행하여 wall-clock 시간을 측정했습니다.

## VM Architecture

- **Zephyr**: NaN-boxed values (8B), register-based bytecode VM, C++23, Clang-cl 빌드
- **Lua 5.5**: NaN-boxed values (8B), register-based VM, C, MSVC 빌드
- **Gravity**: NaN-boxed values, register-based VM, C, MSVC 빌드

## Benchmark Cases

| Case | Description | Workload |
|------|-------------|----------|
| fibonacci(35) | 순수 재귀 — 함수 호출 오버헤드 측정 | fib(35) = ~29.8M 재귀 호출 |
| hot_loop(1M) | 산술 루프 — opcode dispatch 속도 | 100만 회 반복 누적 합계 |
| array_sum(100K) | 배열 생성+합산 — 메모리 할당/인덱싱 | [i, i+1, i+2, i+3] × 100K |
| closure(100K) | 클로저 생성+호출 — upvalue/GC 비용 | make_adder(i)(1) × 100K |
| struct(100K) | 구조체 생성+필드 접근 | Vec2{x,y} × 100K |
| coroutine(100K) | 코루틴 yield/resume | 100 rounds × 1000 yields |

## Results (Clang + Computed Goto, No PGO)

```
======================================================================
  3-Way Benchmark: Lua vs Gravity vs Zephyr
======================================================================
  Case                           Lua       Gravity        Zephyr      Winner
  ─────────────────── ─────────── ───────────── ───────────── ──────
  fibonacci(35)             966ms     2,581ms     1,283ms         Lua
  hot_loop(1M)               17ms        53ms        30ms         Lua
  array_sum(100K)             41ms       101ms        57ms         Lua
  closure(100K)               39ms        88ms        82ms         Lua
  struct(100K)                34ms       118ms        44ms         Lua
  coroutine(100K)            244ms        18ms        82ms     Gravity
======================================================================
```

## Optimization History

fibonacci(35)는 최초 123,229ms에서 1,283ms로 96배 개선되었습니다. 주요 최적화:

1. **반복 프레임 스택**: C++ 재귀 호출 → 반복문 + 프레임 push/pop
2. **레지스터 풀**: per-call 힙 할당 → 사전 할당 풀에서 SP 이동
3. **글로벌 바인딩 flat cache**: Environment 해시맵 탐색 → 1회 resolve 후 Binding* 직접 참조
4. **C computed goto**: switch dispatch → C 파일 분리, 계산된 goto 테이블
5. **R_MAKE_FUNCTION**: 클로저 생성 시 Environment 할당 제거, 레지스터에서 직접 upvalue 캡처
6. **R_RESUME/R_YIELD fiber-style**: 코루틴을 반복 프레임 스택에 통합, 포인터 교환

## Analysis Request

위 벤치마크 결과와 최적화 이력을 기반으로 다음을 분석해주세요:

### 1. 결과 해석
- 각 벤치마크 케이스에서 Zephyr의 상대적 위치를 평가해주세요
- Lua 대비 격차가 큰 케이스(hot_loop 1.82x, closure 2.1x)의 원인을 추정해주세요
- Gravity 대비 coroutine에서만 뒤지는 이유를 분석해주세요

### 2. 추가 최적화 기회
- 현재 아키텍처에서 Lua 수준까지 도달할 수 있는 추가 최적화가 있는지 제안해주세요
- 특히 hot_loop(1.82x)과 closure(2.1x)의 격차를 줄일 방법을 제안해주세요
- coroutine에서 Gravity 수준(18ms)에 도달하기 위한 아키텍처 변경을 제안해주세요

### 3. 벤치마크 방법론 평가
- 현재 벤치마크 케이스가 게임 엔진 워크로드를 충분히 반영하는지 평가해주세요
- 추가해야 할 벤치마크 케이스를 제안해주세요 (예: string 처리, 대규모 테이블, 이벤트 디스패치 등)
- 측정 방법(CLI 실행, wall-clock)의 한계와 개선 방향을 제안해주세요

### 4. 게임 엔진 관점
- 게임 엔진에서 스크립트 언어의 실제 병목이 되는 패턴을 기준으로, 현재 Zephyr의 성능이 실용적으로 충분한 수준인지 평가해주세요
- Lua를 대체하기 위해 반드시 도달해야 하는 성능 기준이 있다면 제시해주세요

### 5. 공정성 검토
- Zephyr(Clang)와 Lua/Gravity(MSVC) 빌드 컴파일러가 다른 점이 결과에 미치는 영향을 분석해주세요
- 벤치마크의 공정성을 높이기 위한 제안을 해주세요

## Source Code References

- **벤치마크 스크립트**: `bench/3way/scripts/` (fib.zph, fib.lua, fib.gravity 등)
- **러너**: `bench/3way/runner.py`
- **VM 핵심 코드**: `src/zephyr_gc_impl.cpp` (execute_register_bytecode)
- **컴파일러**: `src/zephyr_compiler.hpp` (BytecodeCompiler)
- **C dispatch**: `src/zephyr_vm_dispatch.c` (computed goto)
- **PR**: https://github.com/29thnight/Zephyr/pull/3
