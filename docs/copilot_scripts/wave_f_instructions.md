# Wave F — 관찰 가능성 & 성능 심화 구현 지시

## 사전 조건
- `src/zephyr.cpp` (includes all .inl files)
- `src/zephyr_*.inl` (lexer/parser/types/compiler/gc)
- `include/zephyr/api.hpp`
- `tests/tests.cpp`

## 빌드 및 테스트 명령 (각 태스크 완료 후 반드시 실행)
```powershell
msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

## 벤치마크 실행 명령 (Wave F 전체 완료 후 실행)
```powershell
x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
```

---

## F.3 Superinstruction 계측 `[S]` — 먼저 진행

`optimize_bytecode()` 함수에서 superinstruction fusion 횟수를 계측한다.

### 구현 내용

1. **VMStats에 superinstruction 카운터 추가** (`src/zephyr.cpp` 또는 관련 .inl):
   - `vm_stats_` 또는 `RuntimeStats` 구조체에 `superinstruction_fusions` 필드 추가
   - 타입: `uint64_t superinstruction_fusions = 0;`

2. **optimize_bytecode() 내 fusion 시 카운터 증가**:
   - `SIAddStoreLocal`, `SICmpJumpIfFalse`, `SILoadLocalLoadLocal` 등 각 superinstruction 생성 시 `superinstruction_fusions++`

3. **벤치마크 리포트에 superinstruction hit rate 포함**:
   - `bench/bench_runner.cpp` 또는 `bench/main.cpp`에서 VM stats에서 `superinstruction_fusions` 읽어서 출력
   - 출력 형식: `superinstruction_fusions: N (hit_rate: X%)`
   - hit rate = fusions / total_original_opcodes * 100

4. **stats CLI 출력에도 추가** (`cli/main.cpp`):
   - 기존 `barrier_hits`, `lightweight_calls` 출력 옆에 `superinstruction_fusions` 추가

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## F.4 GC Pause Time 계측 `[S]`

GC 이벤트 타임스탬프를 기록하고 pause time percentile을 계산한다.

### 구현 내용

1. **GC 이벤트 기록 구조체 추가** (`src/zephyr_gc.inl` 또는 관련 파일):
   ```cpp
   struct GCPauseRecord {
       uint64_t duration_ns;  // nanoseconds
       bool is_full;          // true=full GC, false=young GC
   };
   ```

2. **GC 컬렉션 함수에 타이밍 추가**:
   - young GC 시작/종료 시 `std::chrono::high_resolution_clock::now()` 기록
   - full GC 시작/종료 시 기록
   - `pause_records_` 벡터에 `GCPauseRecord` 추가 (최대 10,000개 유지, 초과 시 오래된 것 제거)

3. **Percentile 계산 함수 추가**:
   ```cpp
   uint64_t gc_pause_percentile(int pct); // pct = 50, 95, 99
   ```
   - 기록된 pause_records_ 정렬 후 해당 백분위수 반환

4. **frame budget miss 카운터 추가**:
   - `uint64_t frame_budget_miss_count_ = 0;`
   - GC pause가 16ms(60fps 프레임 예산)를 초과하면 카운터 증가

5. **stats CLI 출력에 추가** (`cli/main.cpp`):
   ```
   GC pause p50: Xμs, p95: Yμs, p99: Zμs
   frame_budget_miss: N
   ```

6. **api.hpp에 공개 API 추가**:
   ```cpp
   struct GCPauseStats {
       uint64_t p50_ns;
       uint64_t p95_ns;
       uint64_t p99_ns;
       uint64_t frame_budget_miss_count;
   };
   GCPauseStats get_gc_pause_stats() const;
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## F.5 GC 이벤트 스트림 Export `[M]`

GC 이벤트를 JSON 스트림으로 export한다.

### 구현 내용

1. **GCTraceEvent 구조체 추가**:
   ```cpp
   struct GCTraceEvent {
       enum class Type { YoungStart, YoungEnd, FullStart, FullEnd };
       Type type;
       uint64_t timestamp_ns;  // relative to VM start
       size_t heap_bytes_before;
       size_t heap_bytes_after; // 0 for Start events
   };
   ```

2. **api.hpp에 trace API 추가**:
   ```cpp
   void start_gc_trace();
   void stop_gc_trace();
   bool is_gc_trace_active() const;
   std::string get_gc_trace_json() const; // JSON array of events
   ```

3. **구현**:
   - `gc_trace_active_` bool 플래그
   - `gc_trace_events_` 벡터 (최대 50,000개)
   - GC 함수에서 `gc_trace_active_` 체크 후 이벤트 기록
   - `get_gc_trace_json()`: 이벤트 배열을 JSON 문자열로 직렬화
   - JSON 형식:
     ```json
     [
       {"type":"YoungStart","ts_ns":1234,"heap_before":102400},
       {"type":"YoungEnd","ts_ns":1456,"heap_before":102400,"heap_after":81920},
       ...
     ]
     ```

4. **tests.cpp에 테스트 추가**:
   - `start_gc_trace()` → 스크립트 실행 → `stop_gc_trace()` → `get_gc_trace_json()` 파싱 검증

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## F.6 Coroutine Flame/Trace `[M]`

코루틴 생성/yield/resume/소멸 타임스탬프를 기록한다.

### 구현 내용

1. **CoroutineTraceEvent 구조체 추가**:
   ```cpp
   struct CoroutineTraceEvent {
       enum class Type { Created, Resumed, Yielded, Completed, Destroyed };
       Type type;
       uint64_t coroutine_id;
       uint64_t timestamp_ns;
   };
   ```

2. **api.hpp에 추가**:
   ```cpp
   // ZephyrProfileReport에 coroutine_trace 필드 추가
   struct ZephyrProfileReport {
       // 기존 필드들...
       std::vector<CoroutineTraceEvent> coroutine_trace;  // populated when coroutine tracing enabled
   };
   void start_coroutine_trace();
   void stop_coroutine_trace();
   ```

3. **VM 코루틴 함수에 계측 추가**:
   - 코루틴 생성 시 `Created` 이벤트
   - `resume_coroutine()` 시작 시 `Resumed` 이벤트
   - yield 시 `Yielded` 이벤트
   - 코루틴 완료/소멸 시 `Completed`/`Destroyed` 이벤트
   - `coroutine_trace_active_` bool로 guard (비활성 시 overhead 0)

4. **ZephyrProfileReport에 통합**:
   - `stop_profiling()` 시 coroutine trace도 함께 수집
   - 또는 `stop_coroutine_trace()` 호출 시 별도 반환

5. **tests.cpp에 테스트 추가**:
   - 코루틴 스크립트 실행 후 trace 이벤트 순서 검증 (Created → Resumed → Yielded → Resumed → Completed)

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## F.2 모듈 바이트코드 캐싱 `[M]`

반복 import 시 파싱/컴파일 스킵, 직렬화된 BytecodeFunction 재사용.

### 구현 내용

1. **바이트코드 캐시 구조체 추가**:
   ```cpp
   struct BytecodeCacheEntry {
       std::string source_path;
       uint64_t file_mtime;   // last modified time (stat)
       std::vector<uint8_t> serialized_bytecode;
   };
   ```

2. **VM에 캐시 맵 추가**:
   ```cpp
   std::unordered_map<std::string, BytecodeCacheEntry> bytecode_cache_;
   ```

3. **import 처리 함수 수정**:
   - 모듈 로드 시 캐시 키 = 절대 경로
   - 캐시 히트: `file_mtime` 일치 → 직렬화된 바이트코드 역직렬화 후 사용
   - 캐시 미스: 파싱/컴파일 후 결과를 직렬화해 캐시에 저장
   - 기존 `snapshot()`/`restore_snapshot()` 직렬화 코드 재활용 가능

4. **api.hpp에 캐시 제어 API 추가**:
   ```cpp
   void enable_bytecode_cache(bool enabled = true);
   void clear_bytecode_cache();
   size_t bytecode_cache_size() const; // 캐시된 항목 수
   ```

5. **tests.cpp에 테스트 추가**:
   - 같은 모듈 2회 import 시 2번째는 캐시 히트 확인
   - 파일 변경 시 캐시 무효화 확인

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## F.1 PGO (Profile-Guided Optimization) `[S]`

MSVC PGO를 적용해 switch dispatch 분기 예측을 개선한다.

### 구현 내용

이것은 빌드 시스템 설정 변경이므로 `.vcxproj` 또는 MSBuild 설정을 수정한다.

1. **Zephyr.sln의 프로젝트 설정 확인** (`Zephyr.vcxproj` 또는 유사한 파일):
   - Release x64 구성에 PGO 관련 설정 추가

2. **PGO 3단계 빌드 스크립트 작성** (`docs/copilot_scripts/run_pgo_build.ps1`):
   ```powershell
   # Step 1: Instrument build
   msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGInstrument

   # Step 2: Run benchmark to collect profile data
   x64\Release\zephyr_bench.exe

   # Step 3: Optimize build with profile data
   msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGOptimize
   ```

3. **실제 PGO 적용 여부는 빌드 환경에 따라 다름** — 스크립트만 작성하고 현재 빌드에는 `/GL` + `/LTCG` 플래그만 확인:
   - `.vcxproj`의 Release 구성에 `<WholeProgramOptimization>true</WholeProgramOptimization>` 설정 확인/추가
   - `<LinkTimeCodeGeneration>UseLinkTimeCodeGeneration</LinkTimeCodeGeneration>` 설정 확인/추가

4. **검증**: `/GL` + `/LTCG` 포함된 일반 Release 빌드가 테스트 통과하면 완료

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## 완료 후 처리

1. **벤치마크 실행**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   ```

2. **결과 저장**:
   ```powershell
   # 벤치마크 결과를 wave_f_baseline.json으로 저장
   x64\Release\zephyr_bench.exe --save bench\results\wave_f_baseline.json
   ```

3. **process.md 업데이트** — 완료된 항목을 ✅ 완료로 변경:
   - F.1: PGO (/GL+/LTCG 설정 + PGO 스크립트 작성)
   - F.2: 모듈 바이트코드 캐싱
   - F.3: Superinstruction 계측
   - F.4: GC Pause Time 계측
   - F.5: GC 이벤트 스트림 Export
   - F.6: Coroutine Flame/Trace

## 주의사항

- 각 태스크 완료 후 반드시 `msbuild` + `zephyr_tests.exe` 실행
- 기존 벤치마크 게이트(5/5 PASS)를 유지해야 함
- 성능 오버헤드 있는 기능(GC trace, coroutine trace)은 반드시 bool 플래그로 guard
- 비활성 상태에서는 overhead 0에 가까워야 함
