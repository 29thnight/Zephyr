# H.0 PGO 수동 수집 + 코루틴 레지스터 통합

## 브랜치
현재 브랜치: `feature/register-vm`

## 현재 성능 (wave_h0_optimized.json)
- hot_arithmetic_loop: 2.21ms (~5.5 ns/op)
- coroutine_yield_resume: 317,490 ns (스택 경로)
- 게이트: 5/5 PASS

## MSBuild 경로
```
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
```

---

## Part 1: PGO 수동 프로파일 수집

이전 시도에서 벤치마크 실행 시 `.pgc` 파일이 생성되지 않았다.
이번에는 수동으로 CLI 스크립트를 실행해 프로파일을 수집한다.

### Step 1: vcxproj 확인
`ZephyrRuntime.vcxproj`를 읽어 PGO 설정(`WholeProgramOptimization`, `LinkTimeCodeGeneration`)을 확인하라.

### Step 2: PGO Instrument 빌드
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' ZephyrRuntime.vcxproj /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGInstrument /v:minimal
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' zephyr_bench.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal
```

### Step 3: 임시 Zephyr 스크립트 작성으로 프로파일 수집
`docs/pgo_workload.zph`를 생성:
```
fn fib(n: Int) -> Int {
    if n <= 1 { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn sum_loop(limit: Int) -> Int {
    let total = 0;
    let i = 0;
    while i < limit {
        total = total + i;
        i = i + 1;
    }
    return total;
}

fn main() -> Int {
    let a = fib(20);
    let b = sum_loop(100000);
    return a + b;
}
```

### Step 4: 프로파일 수집 스크립트 작성
`docs/copilot_scripts/collect_pgo.ps1`:
```powershell
Set-Location 'C:\Users\lance\OneDrive\Documents\Project Zephyr'

# 벤치마크로 프로파일 수집
Write-Host "Running benchmark for PGO profile..."
.\x64\Release\zephyr_bench.exe

# CLI로 워크로드 실행
Write-Host "Running CLI workload for PGO profile..."
1..10 | ForEach-Object {
    .\x64\Release\zephyr_cli.exe run docs\pgo_workload.zph
}

# .pgc 파일 확인
Write-Host "Checking for .pgc files..."
Get-ChildItem -Path "x64\Release" -Filter "*.pgc" | Select-Object Name, Length
```

### Step 5: 프로파일 수집 실행
```powershell
powershell -File docs\copilot_scripts\collect_pgo.ps1
```

### Step 6: .pgc 파일 확인
```powershell
Get-ChildItem -Path "x64\Release" -Filter "*.pgc" | Select-Object Name, Length
```

### Step 7: .pgc 파일이 있으면 PGOptimize 빌드
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' ZephyrRuntime.vcxproj /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGOptimize /v:minimal
```

### Step 8: .pgc 파일이 없으면 대안
`ZephyrRuntime.vcxproj`에서 Release 구성에 수동으로 다음 추가:
```xml
<WholeProgramOptimization>true</WholeProgramOptimization>
```
그리고 일반 Release 빌드 진행. `/GL` + `/LTCG`만으로도 일부 최적화 효과 있음.

### Step 9: 빌드 + 테스트
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## Part 2: 코루틴 레지스터 VM 통합

현재 코루틴 포함 모듈은 스택 경로를 사용한다. 단계적으로 레지스터 VM에 통합한다.

### 현재 구조 파악 (먼저 읽기)
1. `src/zephyr_compiler.inl` — 코루틴 가드가 있는 위치 찾기:
   - `uses_register_mode = false` 또는 `is_coroutine_body` 관련 코드
2. `src/zephyr_gc.inl` — `resume_coroutine_single_frame` 또는 `resume_coroutine_bytecode` 함수

### 핵심 구현: CoroutineFrame에 레지스터 파일 추가

**Step 1: CoroutineFrameState 또는 유사 구조체 찾기**
코루틴 프레임의 locals/stack을 저장하는 구조체를 찾아라.

**Step 2: 레지스터 파일 필드 추가**
```cpp
struct CoroutineFrameState {
    // 기존 필드들...
    // 레지스터 VM 지원 추가:
    std::vector<Value> reg_file;   // 레지스터 파일 (uses_register_mode 시)
    size_t reg_ip;                 // 현재 instruction 인덱스
    bool uses_register_mode = false;
};
```

**Step 3: R_YIELD opcode 구현**
`execute_register_bytecode()` 함수에:
```cpp
case R_YIELD: {
    Value yield_val = regs[instr.src1];
    // 현재 상태를 코루틴 프레임에 저장
    coro_frame->reg_file = std::vector<Value>(regs.begin(), regs.end());
    coro_frame->reg_ip = static_cast<size_t>(ip - func->instructions.data());
    coro_frame->uses_register_mode = true;
    // yield 값 반환
    throw YieldException{yield_val}; // 또는 기존 yield 메커니즘 사용
}
```

**Step 4: resume 시 레지스터 복원**
`resume_coroutine_*` 함수에:
```cpp
if (frame->uses_register_mode) {
    // 저장된 레지스터 파일로 execute_register_bytecode 재진입
    return execute_register_bytecode_resumed(func, frame->reg_file, frame->reg_ip);
}
```

**Step 5: 코루틴 가드 해제**
컴파일러에서 코루틴 포함 모듈도 `uses_register_mode = true`로 설정.

**단, 다음 경우는 스택 경로 유지**:
- nested coroutine (코루틴 내에서 다른 코루틴 생성)
- 코루틴 내에서 외부 호스트 함수 yield

### Step 6: 빌드 + 테스트
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```
모든 코루틴 테스트가 통과해야 한다. 실패 시 가드를 복원하고 Part 1 결과만 유지.

---

## 최종 완료 후 처리

1. **벤치마크 실행 + 저장**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   x64\Release\zephyr_bench.exe --output bench\results\wave_h0_final.json
   ```

2. **결과 확인**:
   - hot_arithmetic: 2.21ms → 목표 2.0ms 이하 (5.0 ns/op)
   - coroutine: 317,490 ns → 목표 200,000 ns 이하 (~300 ns/resume)

3. **process.md 업데이트** (`docs/process.md`):
   - H.0 항목에 최종 벤치 수치 기록

## 주의사항
- 코루틴 통합 실패 시: 코루틴 가드 복원하고 Part 1 결과만 적용
- 게이트 5/5 PASS 유지 필수
- 브랜치: `feature/register-vm` 유지
