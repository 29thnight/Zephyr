# Zephyr VM v2 구조 제안 (Next Implementation)

## 목적
현재 Zephyr가 Lua 대비 느려지는 구조적 원인(핫패스 단절, upvalue 함수의 C dispatch 미적용, coroutine 전환 비용)을 해소하기 위해, VM 실행 코어를 재설계한다.

핵심 목표는 다음과 같다.

1. 핫 워크로드를 단일 dispatch 루프에서 끝낸다.
2. closure/coroutine/loop를 모두 fast path에 편입한다.
3. C++ 경로는 예외, deopt, 콜드 케이스 전용으로 축소한다.

## 설계 원칙

1. Single hot loop
- 주 실행 경로는 computed goto 기반 C dispatch 하나로 통일한다.
- opcode 처리 중 C++ 왕복을 최소화한다.

2. Data-oriented frame model
- 함수 프레임과 코루틴 프레임을 유사한 고정 레이아웃으로 정리한다.
- IP, reg_base, reg_count, upvalue_view를 직접 접근 가능한 형태로 유지한다.

3. Fast-path eligibility 확장
- upvalue를 사용하는 함수도 fast path에서 실행 가능해야 한다.
- R_RESUME, R_YIELD를 fast opcode로 승격한다.

4. Compiler-VM co-design
- 루프, 비교, 분기 패턴을 VM 친화적 opcode로 강제 lowering한다.
- quickening(typed opcode 변환) 전제를 둔 baseline bytecode를 설계한다.

## 목표 아키텍처

1. DispatchCore (C)
- 메인 computed goto 루프
- 핫 opcode 직접 처리
- deopt trampoline만 C++ 호출

2. RuntimeState (C++)
- RegisterArena
- CallFrameStack
- OpenUpvalueList
- CoroutinePool
- InlineCache/PIC Store
- GC bridge

3. Compiler Pipeline
- Loop canonicalization
- Super-instruction emission
- Metadata(점프 타겟, 캐시 슬롯) 직렬화

4. Optional Tier (Adaptive Quickening)
- 실행 프로파일 기반 opcode patching
- 실패 시 baseline으로 즉시 rollback

## 우선 KPI

1. Slow-path 진입률
- hot_loop, closure, coroutine에서 slow path 비율을 계측한다.
- 목표: 기존 대비 50% 이상 감소

2. 상대 성능
- hot_loop: Lua 대비 1.3x 이내
- closure: Lua 대비 1.5x 이내
- coroutine: Lua 동급 이상 유지

3. 안정성
- upvalue close 정확성
- coroutine suspend/resume 상태 일관성
- GC mark/relocation 안전성

## 단계별 적용 전략 (요약)

1. Phase A: C dispatch 커버리지 확장
- R_LOAD_UPVALUE, R_STORE_UPVALUE, R_RESUME, R_YIELD 지원

2. Phase B: upvalue/coroutine 데이터 구조 개편
- open/closed upvalue 모델
- coroutine frame 고정 레이아웃

3. Phase C: compiler lowering + super instruction 강제
- 루프 opcode 융합률 향상

4. Phase D: quickening + IC/PIC 고도화
- 타입 특화 opcode 적용
- deopt reason 텔레메트리 구축
