# 코루틴 resume 성능 최적화 (Wave J)

## 브랜치
현재 브랜치: `master`

## 목표
- 현재: ~881 ns/resume (퇴보 상태)
- H.0 기준선: ~634 ns/resume
- 최종 목표: ~300~400 ns/resume

## 빌드 및 테스트 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
x64\Release\zephyr_bench.exe --output bench\results\wave_j_result.json --baseline bench\results\wave_h0_final.json
```

---

## 배경: 현재 퇴보 원인 분석

`bench\results\wave_h0_final.json`은 634 ns/resume, hot_arithmetic 2.17ms를 기록했다.
현재 `bench\results\latest.json`은 881 ns/resume, hot_arithmetic 2.59ms로 퇴보했다.

`module_import` 오피코드 수가 wave_h0_final에서 8개였으나 현재 16개로 증가했다.
이는 Register VM 대신 Stack VM 경로가 실행되고 있음을 나타낸다.

**원인 후보**: 모듈 바이트코드 캐시(Wave F.2에서 구현)가 H.0 이전에 생성된 스택 모드 바이트코드를 반환하고 있을 가능성.

---

## Step 0: 퇴보 진단 및 수정

### 진단
`src/zephyr_gc.inl`에서 모듈 바이트코드 캐시 로직을 찾아라:
- 캐시에서 로드할 때 `uses_register_mode` 불일치 감지 코드가 있는지 확인
- 캐시 파일 경로 패턴 확인 (보통 `.zphc` 또는 유사 확장자)

```powershell
# 캐시 파일이 있다면 삭제
Get-ChildItem -Recurse -Filter "*.zphc" | Remove-Item
Get-ChildItem -Recurse -Filter "*.cache" | Remove-Item
```

### 검증
삭제 후 벤치마크 재실행:
```powershell
x64\Release\zephyr_bench.exe --output bench\results\wave_j_step0.json
```
- `module_import` 오피코드 수가 8개로 줄어야 함
- hot_arithmetic이 2.17ms 근방으로 복구돼야 함
- coroutine이 634 ns/resume 근방으로 복구돼야 함

만약 캐시 파일이 없거나 삭제해도 개선이 없으면:
- `src/zephyr_gc.inl`에서 `execute_register_bytecode` 진입 조건 (`uses_register_mode` 플래그)을 확인
- 컴파일러가 코루틴 함수를 register 모드로 컴파일하고 있는지 `dump-bytecode` 로 확인:
  ```powershell
  x64\Release\zephyr_cli.exe dump-bytecode examples\loops.zph main
  # "register_mode=true" 가 출력돼야 함
  ```

---

## Step 1: 디스패치 루프 내 로컬 포인터 캐싱

### 현재 구조 파악

`src/zephyr_gc.inl`의 `resume_coroutine_single_frame()` 함수를 읽어라.
특히 메인 디스패치 루프 내부:

```cpp
while (frame().ip_index < chunk.instructions.size()) {
    CoroutineFrameState& current_frame = frame();
    const CompactInstruction& instruction = chunk.instructions[current_frame.ip_index];
    // ...
    current_frame.regs[dst] = ...;
    ++current_frame.ip_index;
}
```

### 문제
- `current_frame.regs[dst]`는 `std::vector<Value>`의 heap 포인터를 통해 접근
- 매 opcode마다 `regs.data() + offset` 계산
- `ip_index`가 struct 필드에 있어 매 opcode마다 struct에서 읽고 씀

### 구현

메인 디스패치 루프 진입 직전에 로컬 포인터를 캐싱하라:

```cpp
// 디스패치 루프 시작 직전에 추가
Value* __restrict regs_ptr = frame().regs.data();
const CompactInstruction* instrs_ptr = chunk.instructions.data();
size_t local_ip = frame().ip_index;  // ip를 로컬 변수로 끌어올림
const size_t instrs_count = chunk.instructions.size();

// 루프 조건 변경
while (local_ip < instrs_count) {
    CoroutineFrameState& current_frame = frame();
    const CompactInstruction& instruction = instrs_ptr[local_ip];
    const InstructionMetadata& metadata = chunk.metadata[local_ip];
```

그리고 opcode 핸들러 내부에서:
- `current_frame.regs[dst]` → `regs_ptr[dst]`
- `current_frame.regs[src]` → `regs_ptr[src]`
- `++current_frame.ip_index` → `++local_ip`
- `current_frame.ip_index = target` → `local_ip = target`
- `current_frame.ip_index = static_cast<size_t>(...)` → `local_ip = ...`

### R_YIELD에서 쓰기 백
yield 시 local_ip를 struct에 반영:
```cpp
case BytecodeOp::R_YIELD: {
    frame().ip_index = local_ip + 1;  // +1: yield 다음 명령으로
    // ... 기존 yield 처리 ...
}
```

### R_CALL에서 주의
nested call이 발생하면 frame index가 변할 수 있음.
nested call 후 `regs_ptr`를 갱신해야 할 수 있음:
```cpp
// R_CALL 처리 후
regs_ptr = frame().regs.data();  // frame이 바뀌었을 수 있으므로 재획득
local_ip = frame().ip_index;
```

### 검증
- `msbuild` + `zephyr_tests.exe` (전체 통과)
- 벤치마크: coroutine이 Step 0 기준보다 빨라야 함

---

## Step 2: frame() 람다 제거 — 직접 포인터 사용

### 현재 구조

```cpp
auto frame = [&]() -> CoroutineFrameState& { return coroutine->frames[frame_index]; };
```

이 람다는 hot path에서 반복 호출된다.

### 구현

람다 대신 직접 포인터를 사용:

```cpp
// 람다 제거, 직접 포인터 선언
CoroutineFrameState* current_frame_ptr = &coroutine->frames[frame_index];

// frame() 호출 모두를 *current_frame_ptr 로 교체
// current_frame.xxx → current_frame_ptr->xxx
```

nested call로 frame이 교체될 때 포인터 갱신:
```cpp
// nested call 처리 완료 후
current_frame_ptr = &coroutine->frames[frame_index];
regs_ptr = current_frame_ptr->regs.data();
```

> 단, `frame_index`가 변경되는 모든 경로(push_frame, pop_frame)에서 포인터를 갱신해야 함.
> 안전하게 적용하기 어려우면 Step 2는 건너뛰고 Step 3으로 이동.

### 검증
- `zephyr_tests.exe` 통과
- coroutine 벤치 개선 확인

---

## Step 3: CoroutineFrameState에 small inline 레지스터 배열 (8개)

### 배경
이전 Opt①에서 64개 인라인 배열(512바이트)을 추가했더니 오히려 느려졌다.
벤치마크 코루틴 스크립트의 실제 레지스터 수를 먼저 확인한다:
```powershell
x64\Release\zephyr_cli.exe dump-bytecode examples\<coroutine_script>.zph
# max_regs 값 확인
```

### 구현 (8개 이하면 적용)

만약 `max_regs <= 8` (캐시 라인 친화적 크기)이면:

```cpp
struct CoroutineFrameState {
    // 기존 필드들...

    // 핫 필드를 앞으로 모음 (캐시 라인 최적화)
    size_t ip_index = 0;
    bool uses_register_mode = false;
    std::optional<size_t> pending_call_dst_reg;

    // 소형 레지스터 파일 (8개 = 64바이트)
    static constexpr int kInlineRegs = 8;
    uint8_t reg_count = 0;
    Value inline_regs[kInlineRegs];  // 스택/힙 선택 없는 고정 배열
    std::vector<Value> regs;         // 기존: 9개 이상 시 사용
};
```

`regs`가 비어있고 `reg_count <= kInlineRegs`인 경우 `inline_regs` 사용:
- spawn 시: `max_regs <= kInlineRegs`이면 `reg_count = max_regs`, `inline_regs` 초기화
- 아니면 기존 `regs.assign(...)` 경로 유지

`regs_ptr` 결정:
```cpp
Value* regs_ptr = (current_frame_ptr->reg_count > 0 && current_frame_ptr->regs.empty())
    ? current_frame_ptr->inline_regs
    : current_frame_ptr->regs.data();
```

> 단, GC trace에서 `frame.regs` 루프도 `inline_regs` 케이스를 처리해야 함.
> 각 trace 함수(trace(), trace_young_coroutine())에서 분기 추가 필요.

### 검증
- `zephyr_tests.exe` 전체 통과 (GC stress 테스트 포함)
- coroutine 벤치 개선 확인

---

## Step 4: resume 진입 경로 단순화

### 현재 경로
`resume_coroutine_value()` → `resume_coroutine_bytecode()` → `resume_coroutine_single_frame()`

`resume_coroutine_value()`에서:
- `ModuleRecord fake_module{...}` 생성 (매 resume마다!)
- 코루틴 상태 검증 여러 단계

### 구현

`resume_coroutine_value()`의 `fake_module` 생성을 최소화:
```cpp
// 현재: ModuleRecord fake_module{...}; (멤버 복사 발생)
// 개선: 빈 모듈 레코드를 정적으로 재사용 가능한지 확인

// 또는: CoroutineObject에 cached_module_name 저장 후 재사용
```

`resume_coroutine_bytecode()`의 루프가 단일 프레임인 경우 fast path:
```cpp
RuntimeResult<Runtime::CoroutineExecutionResult> Runtime::resume_coroutine_bytecode(
    CoroutineObject* coroutine, ModuleRecord& module, const Span& call_span) {

    // Fast path: 단일 프레임 (가장 흔한 경우)
    if (coroutine->frames.size() == 1) {
        return resume_coroutine_single_frame(coroutine, module, call_span);
    }

    // 기존 루프 (nested call 처리)
    ...
}
```

### 검증
- `zephyr_tests.exe` 통과
- 최종 벤치마크 저장

---

## 최종 벤치마크

```powershell
x64\Release\zephyr_bench.exe --output bench\results\wave_j_final.json --baseline bench\results\wave_h0_final.json
```

### 게이트 기준 (wave_h0_final 대비)
- coroutine_yield_resume: 634 ns/resume → 300~400 ns/resume 목표
- hot_arithmetic: 2.17ms 유지 (퇴보 없음)
- 5/5 gate PASS 유지

### 각 step별 중간 저장
- Step 0 후: `bench\results\wave_j_step0.json`
- Step 1 후: `bench\results\wave_j_step1.json`
- Step 2 후: `bench\results\wave_j_step2.json`
- Step 3 후: `bench\results\wave_j_step3.json`

---

## 주의사항

1. 각 Step 완료 후 `zephyr_tests.exe` 필수 — GC stress test, nested yield, suspend/resume 포함
2. Step이 성능을 개선하지 못하거나 퇴보시키면 해당 Step만 revert
3. inline_regs(Step 3)는 max_regs가 8 초과인 함수에서 fallback이 반드시 동작해야 함
4. GC trace 수정 시 `gc_verify_full()` 통과가 필수 (메모리 안전성)
5. 브랜치: master 직접 커밋

## 커밋 메시지 형식
```
perf: coroutine resume optimization (Wave J, StepN)

- Brief description of what was changed
- Benchmark result: X ns/resume (was Y ns/resume)
```
