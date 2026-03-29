# 코루틴 resume 최적화 Wave J2

## 브랜치
현재 브랜치: `master`

## 현재 성능 (wave_j_final.json 기준)
- hot_arithmetic: 2.85ms (20.35 ns/op) ← H.0 기준 2.17ms (15.5 ns/op) 대비 31% 퇴보
- coroutine_yield_resume: 587 ns/resume (401 resumes)
  - 실행 비용: ~15 ops * 20 ns = ~305 ns
  - setup/teardown 비용: ~282 ns/resume

## 목표
- hot_arithmetic: 2.17ms (H.0 수준) 복구
- coroutine: 300~400 ns/resume

## 빌드 및 테스트 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
x64\Release\zephyr_bench.exe --output bench\results\wave_j2_result.json --baseline bench\results\wave_h0_final.json
```

---

## Step 0: hot_arithmetic 퇴보 원인 파악 및 수정

### 진단
Wave J 커밋(44343dd)에서 `src/zephyr_compiler.inl`을 변경했다.
어떤 변경이 hot_arithmetic을 느리게 했는지 확인하라.

```powershell
# Wave J 커밋의 compiler.inl 변경 내용 확인
git diff 44343dd~1 44343dd -- src/zephyr_compiler.inl
```

이 변경이 일반 함수 컴파일(non-coroutine)에 영향을 줬는지 확인하라.
특히 `max_regs`, `local_count`, `uses_register_mode` 관련 변경이 있으면 문제일 수 있다.

hot_arithmetic 벤치마크 스크립트가 register 모드로 컴파일되는지 확인:
```powershell
x64\Release\zephyr_cli.exe dump-bytecode bench\scripts\hot_arithmetic.zph 2>&1 | Select-String "register_mode|max_regs"
```
- `register_mode=true` 이어야 정상
- `register_mode=false` 이면 stack VM이 실행 중 (cache 또는 컴파일 경로 문제)

### 수정
만약 Wave J의 compiler.inl 변경이 퇴보 원인이면:
- 해당 변경을 되돌리거나 수정하여 register 컴파일 경로 복구
- `kInlineRegs` 관련 변경이 `compile_register_function` 경로에 영향을 줬다면 제거

만약 모듈 캐시가 오래된 stack 모드 bytecode를 반환하는 경우:
```powershell
# 캐시 파일 삭제
Get-ChildItem -Recurse -Path "C:\Users\lance\OneDrive\Documents\Project Zephyr" -Filter "*.zphc" | Remove-Item -Force
Get-ChildItem -Recurse -Path "C:\Users\lance\OneDrive\Documents\Project Zephyr" -Filter "*.bcache" | Remove-Item -Force
```

빌드 + 벤치마크 재확인:
```powershell
x64\Release\zephyr_bench.exe --output bench\results\wave_j2_step0.json --baseline bench\results\wave_h0_final.json 2>&1 | Select-String "hot_arithmetic|coroutine_resume"
```
hot_arithmetic이 2.17ms 근방으로 복구돼야 한다.

---

## Step 1: 전용 레지스터 모드 코루틴 resume fast path

### 배경
현재 `resume_coroutine_single_frame()`은 stack 모드와 register 모드를 하나의 함수에서 처리한다.
dispatch 루프 내에서 `if (uses_register_mode)` 분기가 존재한다.
레지스터 모드 전용 코루틴 resume 함수를 분리하면 이 분기가 제거된다.

### 현재 구조 파악
`src/zephyr_gc.inl`에서 `resume_coroutine_single_frame()`을 읽어라.
특히:
- 함수 시작부의 초기화 코드 (stack/locals 초기화 vs regs 초기화)
- dispatch 루프 내 `if (frame_ptr->uses_register_mode)` 분기
- stack 모드 전용 opcode 케이스 (PUSH, POP, LOAD_LOCAL 등)

### 구현

새 함수 `resume_register_coroutine_fast(CoroutineObject*, const Span&)` 추가:

```cpp
RuntimeResult<Runtime::CoroutineExecutionResult>
Runtime::resume_register_coroutine_fast(CoroutineObject* coroutine, const Span& call_span) {
    // frame_ptr 직접 획득 (이미 Wave J에서 frame_ptr 사용 중)
    CoroutineFrameState* frame_ptr = &coroutine->frames[coroutine->frames.size() - 1];
    const BytecodeFunction& chunk = *frame_ptr->bytecode;

    // regs_ptr, instrs_ptr, local_ip는 이미 Wave J에서 캐싱됨
    // 이 함수에서는 stack 모드 관련 초기화를 완전히 제거

    Value* regs_ptr;
    if (frame_ptr->reg_count > 0 && frame_ptr->regs.empty()) {
        regs_ptr = frame_ptr->inline_regs;  // kInlineRegs 사용
    } else {
        regs_ptr = frame_ptr->regs.data();
    }

    const CompactInstruction* instrs_ptr = chunk.instructions.data();
    const size_t instrs_count = chunk.instructions.size();
    size_t local_ip = frame_ptr->ip_index;

    // register-only dispatch loop (R_* opcodes만)
    while (local_ip < instrs_count) {
        const CompactInstruction& instr = instrs_ptr[local_ip];
        switch (instr.op) {
        case BytecodeOp::R_LOAD_CONST: { ... }
        case BytecodeOp::R_MOV: { ... }
        case BytecodeOp::R_ADD: { ... }
        // ... 모든 R_* opcodes
        case BytecodeOp::R_YIELD: {
            frame_ptr->ip_index = local_ip + 1;
            return CoroutineExecutionResult{CoroutineStatus::Suspended, regs_ptr[src]};
        }
        case BytecodeOp::R_RETURN: {
            frame_ptr->ip_index = local_ip;
            return CoroutineExecutionResult{CoroutineStatus::Completed, regs_ptr[src]};
        }
        }
        ++local_ip;
    }
}
```

> 이 함수를 새로 작성하는 것이 어렵다면, 기존 `resume_coroutine_single_frame()`에서
> 레지스터 모드로 확인된 경우 stack 관련 초기화를 완전히 스킵하는 early-exit 추가:
> ```cpp
> if (frame_ptr->uses_register_mode) {
>     // stack/locals 초기화 블록 전체 스킵
>     goto register_dispatch_loop;
> }
> ```

### resume_coroutine_bytecode()에서 분기
```cpp
RuntimeResult<Runtime::CoroutineExecutionResult>
Runtime::resume_coroutine_bytecode(CoroutineObject* coroutine, ...) {
    // 단일 프레임 + 레지스터 모드: 초고속 경로
    if (coroutine->frames.size() == 1 &&
        coroutine->frames[0].uses_register_mode) {
        return resume_register_coroutine_fast(coroutine, call_span);
    }
    // 기존 경로 (nested frame, stack mode)
    ...
}
```

### 검증
- `zephyr_tests.exe` 전체 통과 (GC stress, nested yield, suspend/resume)
- 벤치마크 확인

---

## Step 2: resume_coroutine_value() setup 비용 절감

### 현재 구조 파악
`src/zephyr_gc.inl`에서 `resume_coroutine_value()` 함수를 읽어라.
매 resume마다 생성되는 객체/구조체를 파악하라.

특히:
- `ModuleRecord fake_module{...}` 또는 유사한 임시 구조체
- string 복사 (module_name 등)
- 코루틴 상태 검증 코드

### 구현

코루틴 handle을 통한 resume hot path 추가:

```cpp
RuntimeResult<Value> Runtime::resume_coroutine_value(
    const Value& value, const Span& span, const std::string& module_name) {

    // fast path: CoroutineObject 직접 추출
    auto* coro = value.as_object<CoroutineObject>();
    if (LIKELY(coro && coro->status == CoroutineStatus::Suspended &&
               !coro->frames.empty() &&
               coro->frames.size() == 1 &&
               coro->frames[0].uses_register_mode)) {

        // fake_module 생성 없이 직접 resume
        Span empty_span{};
        auto result = resume_register_coroutine_fast(coro, empty_span);
        if (!result) return result.error();
        // 결과 처리...
        return result->value;
    }

    // slow path: 기존 로직
    ...
}
```

> `fake_module`이 실제로 사용되는 경우(error reporting 등)는 slow path에서 처리.
> fast path는 yield 값만 반환하면 됨.

### 검증
- `zephyr_tests.exe` 통과
- 벤치마크 확인

---

## Step 3: R_ADD/R_MOV 패스트패스에서 타입 체크 제거

### 현재 구조 파악
register dispatch 루프에서 R_ADD, R_SUB, R_MUL 처리 코드를 읽어라:
```powershell
$lines = Get-Content "src\zephyr_gc.inl"
# R_ADD 처리 코드 확인
```

만약 `binary_fast_or_fallback()` 호출이 있다면, 이 함수 내부에서 타입 체크를 수행한다.

### 구현

정수 fast path를 먼저 시도:
```cpp
case BytecodeOp::R_ADD: {
    const Value& v1 = regs_ptr[src1];
    const Value& v2 = regs_ptr[src2];
    // Int fast path (가장 흔한 케이스)
    if (LIKELY(v1.is_int() && v2.is_int())) {
        regs_ptr[dst] = Value::integer(v1.as_int() + v2.as_int());
        ++local_ip;
        break;
    }
    // Float fast path
    if (v1.is_float() && v2.is_float()) {
        regs_ptr[dst] = Value::number(v1.as_float() + v2.as_float());
        ++local_ip;
        break;
    }
    // Fallback (문자열 연결 등)
    ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(instr.op, v1, v2, span));
    regs_ptr[dst] = result;
    ++local_ip;
    break;
}
```

이미 이런 fast path가 있다면 스킵.

---

## 최종 벤치마크 및 커밋

```powershell
x64\Release\zephyr_bench.exe --output bench\results\wave_j2_final.json --baseline bench\results\wave_h0_final.json
```

### 게이트 기준 (wave_h0_final 대비)
- coroutine_yield_resume: 587 ns → 300~400 ns/resume 목표
- hot_arithmetic: 2.85ms → 2.17ms 복구 목표
- 5/5 gate PASS 유지

### 중간 저장
- Step 0 후: `bench\results\wave_j2_step0.json`
- Step 1 후: `bench\results\wave_j2_step1.json`
- Step 2 후: `bench\results\wave_j2_step2.json`

---

## 주의사항

1. Step 0 (hot_arithmetic 복구) 없이 Step 1~3 진행 가능 — 별개 문제
2. Step 1의 `resume_register_coroutine_fast` 구현 시:
   - R_CALL (nested function call) 처리가 복잡하면 기존 경로에 fallback
   - GC safepoint check는 yield/call 시에만 수행
3. 각 step 후 `zephyr_tests.exe` 필수

## 커밋 메시지
```
perf: coroutine resume optimization (Wave J2)
- hot_arithmetic regression fix (Step 0)
- register-mode coroutine fast path (Step 1)
- resume_coroutine_value() setup cost reduction (Step 2)
- Benchmark: X ns/resume (was 587 ns/resume)
```
