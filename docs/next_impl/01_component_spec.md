# 1. 상세 컴포넌트 명세서 (VM v2)

## 1) DispatchCore (C 레이어)

### 책임
- register-mode bytecode의 메인 실행 루프를 담당한다.
- 핫 opcode를 직접 처리하고, 실패 시 deopt 코드로 상위 레이어에 제어를 넘긴다.

### 입력 상태
- regs 포인터
- instruction 포인터
- ip
- active frame 정보
- globals cache
- upvalue view
- coroutine state

### 출력/반환 규약
- ZVM_CONTINUE: 루프 내부 계속 실행
- ZVM_RETURN: 함수 반환 완료
- ZVM_DEOPT: 상위(C++)에서 콜드 처리 필요
- ZVM_ERROR: 런타임 에러

### 필수 지원 opcode
- 산술/분기: R_ADD, R_SUB, R_MUL, R_ADDI, R_JUMP, R_JUMP_IF_FALSE 등
- 클로저: R_MAKE_FUNCTION, R_LOAD_UPVALUE, R_STORE_UPVALUE
- 코루틴: R_MAKE_COROUTINE, R_RESUME, R_YIELD
- 접근: R_LOAD_GLOBAL, R_LOAD_MEMBER, R_STORE_MEMBER, R_LOAD_INDEX

### 비기능 요구사항
- jump target은 C 레이어에서 직접 복원 가능해야 한다.
- 메타데이터 의존으로 인한 불필요한 slow path를 금지한다.

## 2) Frame Model

### 공통 FrameHeader
- chunk pointer
- ip index
- reg_base
- reg_count
- dst register (return slot)
- flags (same_func, uses_register_mode, coroutine_frame)

### CallFrame
- 일반 함수 호출용
- active module/env, upvalue view 참조

### CoroutineFrame
- suspend/resume를 위한 저장 상태
- yielded 값 슬롯
- resume 호출 횟수, yield 횟수

### 설계 포인트
- call frame과 coroutine frame은 동일한 필드를 최대한 공유한다.
- frame 교체는 복사보다 포인터 스왑을 우선한다.

## 3) Upvalue Subsystem

### 모델
- OpenUpvalue: 현재 스택/레지스터 슬롯을 참조
- ClosedUpvalue: 힙에 승격된 셀 보관

### API
- capture_upvalue(slot)
- close_upvalues(frame_base)
- load_upvalue(id)
- store_upvalue(id, value)

### 요구사항
- 함수 반환/예외 unwind/코루틴 suspend 경로 모두에서 close 프로토콜이 동일해야 한다.
- GC 마킹 시 open/closed 양쪽을 정확히 추적해야 한다.

## 4) Coroutine Engine

### 상태 머신
- New -> Running -> Suspended -> Running -> Completed

### fast-path resume/yield
- resume: caller frame push 후 coroutine frame으로 스왑
- yield: coroutine frame ip 저장 후 caller frame 복귀
- 공통: 메모리 재할당 없이 프레임/레지스터 윈도우 교체

### 제약
- nested coroutine은 초기에는 slow path 유지 가능
- single-frame register coroutine부터 완전 fast path 적용

## 5) Compiler Lowering

### Loop Canonicalization
- i = i + step + cmp + jump 패턴을 R_SI_* 계열로 축약
- while/for를 동일한 내부 루프 형태로 정규화

### Closure Lowering
- upvalue slot map을 정적 인덱스로 부여
- R_MAKE_FUNCTION의 캡처 테이블을 압축 표현

### Metadata Layout
- jump target, inline cache slot, const cache 정보를 고정 위치에 저장
- C 레이어에서 직접 읽을 수 있는 구조를 보장

## 6) Inline Cache / PIC

### 1차 목표
- global/member/index 접근의 monomorphic cache

### 2차 목표
- polymorphic inline cache (최대 N개 shape)

### 실패 처리
- cache miss 시 deopt 후 C++에서 재해결
- 성공 시 opcode site에 캐시 갱신

## 7) GC Interface

### 추적 루트
- RegisterArena active window
- CallFrameStack
- CoroutinePool frames
- OpenUpvalueList

### write barrier
- upvalue store/member store/coroutine frame update 경로에 적용

### 안정성 점검 포인트
- coroutine frame 내부 raw pointer 제거 또는 안정 참조 규약 수립
- compaction 시 포워딩 테이블 업데이트 규칙 명문화

## 8) Telemetry/Observability

### 필수 카운터
- opcode 실행 횟수
- slow path 진입 횟수 및 reason 코드
- deopt 횟수
- resume/yield fast-path 비율

### 출력 포맷
- bench 러너가 JSON으로 수집 가능한 구조 제공
- 케이스별 p50/p95/min/max 실행 시간 기록
