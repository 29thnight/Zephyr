# H.0 코루틴 통합 퇴보 원인 분석 및 수정

## 브랜치
현재 브랜치: `feature/register-vm`

## 현재 상황
코루틴 레지스터 통합 후 hot_arithmetic_loop이 2.21ms → 2.61ms로 18% 퇴보.
원인: `pending_call_dst_reg` 추가 + 코루틴 가드 제거 → R_CALL마다 오버헤드 발생 추정.

## 목표
퇴보 없이 코루틴 레지스터 통합을 유지하거나, 퇴보 원인을 제거하여 2.21ms 이하 달성.

## MSBuild 경로
```
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
```

---

## Step 1: 퇴보 원인 정확히 파악

### 1-1. execute_register_bytecode()의 R_CALL 케이스 읽기
`src/zephyr_gc.inl`의 `execute_register_bytecode()` 함수에서 `R_CALL` 처리 코드를 읽어라.
특히 `pending_call_dst_reg`가 어떻게 사용되는지 확인.

### 1-2. 코루틴 관련 경로 확인
`resume_coroutine_bytecode()` 또는 유사 함수에서 `pending_call_dst_reg` 사용 부분을 읽어라.

### 1-3. dump-bytecode로 hot_arithmetic 바이트코드 확인
```powershell
$script = @'
fn hot_loop(limit: Int) -> Int {
    let total = 0;
    let i = 0;
    while i < limit {
        total = total + i;
        i = i + 1;
    }
    return total;
}
'@
$script | Out-File -Encoding utf8 -FilePath "docs\hot_check.zph"
.\x64\Release\zephyr_cli.exe dump-bytecode docs\hot_check.zph hot_loop
```

---

## Step 2: 핵심 최적화 원칙

R_CALL이 hot path에서 실행될 때 코루틴 관련 오버헤드를 **0**으로 만들어야 한다.

### 방법 A: 코루틴 컨텍스트 외부에서는 pending_call_dst_reg 체크 제거
```cpp
case R_CALL: {
    // is_coroutine_frame은 외부에서 한 번만 체크
    // hot path: coroutine이 아닐 때 pending_call_dst_reg 로직 완전 스킵
    Value result = call_function(regs[instr.src1], ...);
    regs[instr.dst] = result;
    // 코루틴 케이스는 exception/longjmp로만 처리
    break;
}
```

### 방법 B: execute_register_bytecode()를 두 버전으로 분리
```cpp
// 1. 코루틴 없는 일반 버전 (오버헤드 0)
Value execute_register_bytecode(BytecodeFunction* func, Value* args, int argc);

// 2. 코루틴 컨텍스트 버전 (yield/resume 지원)
Value execute_register_bytecode_coro(BytecodeFunction* func, CoroutineFrame* frame, ...);
```
- 일반 함수 호출: 버전 1 사용 → 코루틴 오버헤드 없음
- 코루틴 frame 실행: 버전 2 사용
- **이 방법이 가장 확실한 해결책**

### 방법 C: inline hint + __assume 추가
```cpp
case R_CALL: {
    Value result = call_function(...);
    regs[instr.dst] = result;
    // pending_call_dst_reg는 yield exception path에서만 접근
    // → MSVC: __assume(0) 또는 [[unlikely]] hint
    break;
}
```

---

## Step 3: 방법 B 구현 (권장)

### 3-1. execute_register_bytecode_coro() 분리

현재 `execute_register_bytecode()`에서 코루틴 관련 로직(`pending_call_dst_reg`, `coro_frame`, yield 처리)을 새 함수로 분리:

```cpp
// 일반 함수용 (코루틴 오버헤드 없음)
Value execute_register_bytecode(BytecodeFunction* func, Value* args, int argc) {
    std::vector<Value> regs(func->max_regs);
    for (int i = 0; i < argc; i++) regs[i] = args[i];

    const auto* ip = func->instructions.data();
    // ... dispatch loop (코루틴 관련 코드 없음)
    // R_YIELD → 일반 함수에서는 오류 또는 무시
    // R_CALL → 그냥 call_function()만 호출
}

// 코루틴용
Value execute_register_bytecode_coro(
    BytecodeFunction* func,
    CoroutineFrameState* frame,
    std::vector<Value>& regs,  // frame에서 복원된 레지스터
    size_t start_ip
) {
    // yield/resume 처리 포함
    // pending_call_dst_reg 등 코루틴 전용 로직
}
```

### 3-2. 호출 경로 분기
- 일반 함수 호출 시: `execute_register_bytecode()` (빠른 버전)
- 코루틴 resume 시: `execute_register_bytecode_coro()` (코루틴 버전)

### 3-3. R_CALL에서 nested yield 처리
일반 `execute_register_bytecode()`의 R_CALL에서 callee가 코루틴이면 코루틴 버전으로 전환:
```cpp
case R_CALL: {
    if (callee->is_coroutine_function()) {
        // 코루틴 spawn → 코루틴 처리 경로
    } else {
        // 일반 호출 → 재귀적으로 execute_register_bytecode() 호출
        regs[instr.dst] = execute_register_bytecode(callee, &regs[src_start], argc);
    }
    break;
}
```

---

## Step 4: 빌드 + 벤치마크 비교

### 4-1. 수정 후 빌드
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

### 4-2. 테스트
```powershell
x64\Release\zephyr_tests.exe
```
모든 테스트 통과 필수 (코루틴 테스트 포함).

### 4-3. 벤치마크
```powershell
x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
```

목표:
- hot_arithmetic_loop: 2.21ms 이하 (퇴보 해소)
- coroutine_yield_resume: 317,490 ns 이하 (퇴보 해소)
- 5/5 PASS

### 4-4. 성공 시 저장
```powershell
x64\Release\zephyr_bench.exe --output bench\results\wave_h0_final.json
```

---

## 실패 시 대안

위 방법이 실패하면 다음을 시도:

**대안 1**: pending_call_dst_reg를 CoroutineFrameState 대신 thread_local에 저장 (cache miss 감소)

**대안 2**: R_CALL에서 코루틴 여부를 BytecodeFunction의 `has_yield` 플래그로만 체크:
```cpp
case R_CALL: {
    Value result = call_function(...);
    if (UNLIKELY(result.is_yield_sentinel())) {
        // yield propagation
    } else {
        regs[instr.dst] = result;
    }
    break;
}
```
`UNLIKELY` 힌트로 분기 예측 개선.

**대안 3**: 코루틴 통합 포기 → wave_h0_optimized.json (2.21ms) 상태로 revert
```powershell
# 코루틴 가드 복원만 진행
```

## 주의사항
- 코루틴 테스트 전체 통과 필수
- 퇴보 해소 안되면 대안 3으로 fallback
- 브랜치: `feature/register-vm`
