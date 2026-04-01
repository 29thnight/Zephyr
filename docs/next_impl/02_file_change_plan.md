# 2. 파일 단위 변경 계획 (실행 순서 기준)

## 범위
본 계획은 기존 코드베이스를 기준으로 VM v2 구조로 점진 전환하기 위한 파일별 작업 지침이다.

## Phase A: Dispatch 커버리지 확장 (최소 침습)

### src/zephyr_vm_dispatch.h
작업
1. ZDispatchState에 upvalue view, coroutine fast-path 필드 추가
2. deopt reason enum 추가
3. R_RESUME/R_YIELD용 콜백 또는 반환 코드 정의

완료 기준
- 기존 빌드 영향 없이 새로운 상태 필드가 C/C++ 양쪽에서 접근 가능

### src/zephyr_vm_dispatch.c
작업
1. dispatch_table에 R_LOAD_UPVALUE, R_STORE_UPVALUE, R_RESUME, R_YIELD 추가
2. R_SI_CMP_JUMP_FALSE false-branch를 C에서 직접 jump 처리
3. slow_path 진입 reason 코드 기록

완료 기준
- closure/coroutine 벤치에서 slow_path 비율이 즉시 감소

### src/zephyr_gc_impl.cpp
작업
1. C dispatch 진입 제한 조건 완화(업밸류 함수 허용)
2. 신규 deopt reason 처리 및 재진입 로직 연결
3. R_RESUME/R_YIELD fast opcode와 상태 동기화

완료 기준
- 기능 회귀 없이 closure/coroutine 동작 동일

## Phase B: Upvalue/Coroutine 구조 개편

### src/zephyr_internal.hpp
작업
1. OpenUpvalue/ClosedUpvalue 구조 정의
2. FrameHeader/CallFrame/CoroutineFrame 공통 필드 정리
3. raw pointer 안정성 규약(핸들/인덱스 기반) 추가

완료 기준
- 새 구조로 빌드 및 런타임 타입 점검 통과

### src/zephyr_gc_impl.cpp
작업
1. capture/close/load/store upvalue 경로 분리 구현
2. coroutine frame 저장/복원 로직을 포인터 스왑 중심으로 개편
3. GC trace 루트에 OpenUpvalueList, CoroutinePool 반영

완료 기준
- tests/test_gc.cpp, tests/test_vm.cpp, tests/test_host.cpp 통과

## Phase C: Compiler Lowering 및 Super Instruction

### src/zephyr_compiler.hpp
작업
1. loop canonicalization 패스 추가
2. while/for lowering 통일
3. jump target/ic slot을 C-friendly metadata로 저장

완료 기준
- 루프 케이스에서 R_SI_* opcode 비중 상승

### tests/test_compiler.cpp
작업
1. canonical loop lowering golden test 추가
2. closure upvalue slot mapping 검증 테스트 추가

완료 기준
- 바이트코드 형태 회귀 자동 검출 가능

## Phase D: Bench/Telemetry 확장

### bench/3way/runner.py
작업
1. warmup + 반복 측정 + min/p50/p95 집계 추가
2. VM telemetry JSON 병합 수집
3. compile_time/execute_time 분리 기록

완료 기준
- 단일 실행 wall-clock 외에 반복 통계 제공

### bench/3way/scripts/*
작업
1. native callback/event dispatch/string 처리 케이스 추가
2. 기존 케이스는 회귀 비교 기준선으로 유지

완료 기준
- 게임 엔진 패턴 반영 벤치 세트 확보

## 교차 검증 작업

### tests/test_vm.cpp
작업
1. fast path와 slow path 결과 동등성 검증
2. deopt 전후 레지스터/리턴값 일치 검증

### tests/test_perf.cpp
작업
1. hot_loop/closure/coroutine 성능 가드레일 추가
2. 임계값 기반 CI 경고(하드 실패는 선택)

## 마이그레이션 원칙

1. 동작 동일성 우선
- 각 Phase는 의미 보존을 최우선으로 한다.

2. 계측 선행
- 최적화 전후에 slow-path ratio를 반드시 기록한다.

3. 롤백 가능성 확보
- 플래그 기반으로 신규 fast opcode 경로를 단계적 활성화한다.

## 최종 인수 조건

1. 정확성
- 기존 테스트 전량 통과
- 코루틴/클로저 회귀 없음

2. 성능
- hot_loop Lua 대비 1.3x 이내
- closure Lua 대비 1.5x 이내

3. 가시성
- deopt reason 및 slow-path 통계가 벤치 결과와 함께 출력됨
