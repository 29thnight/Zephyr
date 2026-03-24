# Wave G — 개발자 경험 개선 구현 지시

## 빌드 및 테스트 명령 (각 태스크 완료 후 반드시 실행)
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## G.2 check 단계 강화 `[M]`

컴파일러의 `check` 단계에서 더 많은 정적 검증을 수행한다.

### 구현 내용

1. **trait 구현 불일치 진단** (`src/zephyr_compiler.inl` 또는 `src/zephyr_gc.inl`):
   - `impl TraitName for StructName` 선언 처리 시, trait에 선언된 메서드가 impl 블록에 모두 있는지 검사
   - 누락된 메서드가 있으면 컴파일 오류:
     ```
     error: impl of 'TraitName' for 'StructName' is missing method 'method_name'
     ```
   - impl 블록에 trait에 없는 메서드가 있으면 경고:
     ```
     warning: method 'extra_method' is not part of trait 'TraitName'
     ```

2. **함수 시그니처 불일치 오류 메시지 개선**:
   - 현재: 인자 수/타입 불일치 시 generic 오류
   - 개선: "function 'foo' expects 2 arguments, got 3" 형식으로 구체화
   - 호출 위치(line/col)와 정의 위치를 함께 표시

3. **optional chaining nil 전파 경고**:
   - `?.` 결과를 nil 체크 없이 바로 메서드 호출하는 경우 경고
   - 예: `obj?.field.method()` → `obj?.field`가 nil일 때 `.method()`가 크래시 위험

4. **import/export 경계 타입 일관성 검사**:
   - `import` 시 모듈이 export하지 않는 심볼 참조 → 컴파일 오류
   - "module 'foo' does not export 'bar'" 형식

### 검증
- tests.cpp에 진단 테스트 추가 (잘못된 impl → 오류 확인, 올바른 impl → 정상 컴파일)
- `msbuild` + `zephyr_tests.exe` 통과

---

## G.3 오류 메시지 품질 개선 `[M]`

런타임 오류에 더 풍부한 문맥을 제공한다.

### 구현 내용

1. **런타임 타입 오류에 스택 트레이스 포함**:
   - `ZephyrRuntimeError` 또는 유사 예외에 call stack 정보 추가
   - VM 실행 중 오류 발생 시 현재 call frame 체인을 역추적
   - 출력 형식:
     ```
     RuntimeError: expected Int, got String
       at foo (script.zph:12)
       at bar (script.zph:8)
       at <main> (script.zph:1)
     ```
   - api.hpp의 오류 반환 타입 또는 exception에 `stack_trace` 필드 추가

2. **pattern matching exhaustiveness 힌트**:
   - `match` 표현식이 모든 경우를 커버하지 않을 때 컴파일/런타임 경고
   - "match may not cover all cases: missing nil, String patterns" 형식

3. **trait method not found 시 impl 누락 안내**:
   - 런타임에서 trait 메서드 디스패치 실패 시:
     ```
     RuntimeError: 'StructName' does not implement method 'method_name'
     hint: add 'impl TraitName for StructName { fn method_name(...) { ... } }'
     ```

### 검증
- tests.cpp에 오류 메시지 포맷 검증 테스트 추가
- `msbuild` + `zephyr_tests.exe` 통과

---

## G.4 Bytecode Dump 연동 `[S]`

`dump-bytecode` 출력과 테스트를 연결해 회귀를 자동 감지한다.

### 구현 내용

1. **dump-bytecode CLI 커맨드에 superinstruction 표시 추가** (`cli/main.cpp`):
   - 현재 `dump-bytecode` 출력에 superinstruction 타입 표시
   - 예: `[SI] SIAddStoreLocal r0, r1, r2` 형식

2. **tests.cpp에 bytecode dump 회귀 테스트 추가**:
   - 특정 스크립트의 바이트코드 패턴을 검증하는 테스트
   - 예: 간단한 `for` 루프가 superinstruction으로 퓨전되는지 확인
   - `vm.dump_bytecode(source)` 또는 유사 API 사용
   - 예상 opcode 시퀀스 포함 여부 assert

3. **api.hpp에 bytecode dump API 추가** (없으면):
   ```cpp
   std::string dump_bytecode(const std::string& source);
   // Returns human-readable bytecode listing
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## G.5 Corpus 기반 회귀 테스트 `[M]`

`tests/corpus/*.zph` 스크립트 집합을 추가하고 예상 출력을 검증한다.

### 구현 내용

1. **corpus 디렉토리 생성** `tests/corpus/`:

2. **스크립트 파일 추가** (최소 6개):

   `tests/corpus/01_basic_arithmetic.zph`:
   ```
   fn main() {
       let x = 10;
       let y = 20;
       print(x + y);
       print(x * y - 5);
   }
   ```
   예상 출력: `30\n195`

   `tests/corpus/02_string_interpolation.zph`:
   ```
   fn main() {
       let name = "Zephyr";
       let ver = 1;
       print(f"Hello {name} v{ver}");
   }
   ```
   예상 출력: `Hello Zephyr v1`

   `tests/corpus/03_optional_chaining.zph`:
   ```
   struct Node { value: Int, next: Node? }
   fn main() {
       let n = Node { value: 42, next: nil };
       print(n?.value);
       print(n?.next?.value);
   }
   ```
   예상 출력: `42\nnil`

   `tests/corpus/04_pattern_matching.zph`:
   ```
   fn classify(x: Int) -> String {
       match x {
           0 => "zero",
           1 | 2 | 3 => "small",
           n if n < 0 => "negative",
           _ => "large",
       }
   }
   fn main() {
       print(classify(0));
       print(classify(2));
       print(classify(-5));
       print(classify(100));
   }
   ```
   예상 출력: `zero\nsmall\nnegative\nlarge`

   `tests/corpus/05_coroutine.zph`:
   ```
   coroutine fn counter(limit: Int) {
       let i = 0;
       while i < limit {
           yield i;
           i = i + 1;
       }
   }
   fn main() {
       let c = counter(3);
       print(resume c);
       print(resume c);
       print(resume c);
   }
   ```
   예상 출력: `0\n1\n2`

   `tests/corpus/06_traits.zph`:
   ```
   trait Greet {
       fn greet(self) -> String;
   }
   struct Cat { name: String }
   impl Greet for Cat {
       fn greet(self) -> String {
           f"Meow, I am {self.name}"
       }
   }
   fn main() {
       let c = Cat { name: "Kitty" };
       print(c.greet());
   }
   ```
   예상 출력: `Meow, I am Kitty`

3. **tests.cpp에 corpus 테스트 runner 추가**:
   ```cpp
   void test_corpus_scripts() {
       // 각 corpus 파일 실행 후 출력 검증
       struct CorpusCase {
           std::string path;
           std::string expected_output;
       };
       std::vector<CorpusCase> cases = {
           {"tests/corpus/01_basic_arithmetic.zph", "30\n195"},
           {"tests/corpus/02_string_interpolation.zph", "Hello Zephyr v1"},
           // ...
       };
       for (auto& c : cases) {
           // vm.execute_file(c.path) 또는 vm.execute(read_file(c.path))
           // captured stdout 또는 print callback으로 출력 비교
       }
   }
   ```

4. **print 출력 캡처 방법**:
   - VM에 `set_print_handler(std::function<void(const std::string&)>)` API가 있으면 사용
   - 없으면 api.hpp에 추가하거나, execute 결과값으로 검증

### 검증
- `msbuild` + `zephyr_tests.exe` 통과 (모든 corpus 스크립트 정상 실행)

---

## G.1 테스트 체계 세분화 `[M]`

tests.cpp 단일 파일을 기능군별로 분리한다.

### 구현 내용

현재 `tests/tests.cpp` (~2,900줄)를 다음 파일들로 분리:

1. **파일 구조**:
   - `tests/test_lexer.cpp` — 렉서/파서 관련 테스트
   - `tests/test_compiler.cpp` — 컴파일러/바이트코드 관련 테스트
   - `tests/test_vm.cpp` — VM 실행/값 관련 테스트
   - `tests/test_gc.cpp` — GC/메모리 관련 테스트 (Wave C~F 포함)
   - `tests/test_host.cpp` — host binding/handle 관련 테스트
   - `tests/test_perf.cpp` — 성능 관련 테스트
   - `tests/test_corpus.cpp` — corpus 기반 회귀 테스트 (G.5에서 추가한 내용 이동)
   - `tests/test_main.cpp` — main() 진입점, 모든 테스트 함수 호출

2. **분리 방법**:
   - 각 테스트 함수를 적절한 파일로 이동
   - 공통 헤더/fixture는 `tests/test_common.hpp`로 추출
   - 각 .cpp 파일의 함수 선언은 test_common.hpp 또는 각 헤더에 추가

3. **MSBuild 프로젝트 업데이트** (`zephyr_tests.vcxproj`):
   - 새 .cpp 파일들을 `<ClCompile>` 항목에 추가
   - 기존 `tests.cpp`는 제거 또는 빈 파일로 유지

4. **분리 후 기존 tests.cpp 제거 또는 빈 파일로 교체**

### 주의사항
- 분리 전후 동일한 테스트가 실행되어야 함
- 각 파일이 독립적으로 컴파일 가능해야 함

### 검증
- `msbuild` + `zephyr_tests.exe` 통과 (동일한 테스트 수 통과)

---

## 완료 후 처리

1. **빌드 + 테스트 최종 확인**:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
   x64\Release\zephyr_tests.exe
   ```

2. **벤치마크 게이트 유지 확인**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   ```

3. **process.md 업데이트** — Wave G 항목을 ✅ 완료로 변경:
   ```
   | G.1 | 테스트 체계 세분화 | ✅ 완료 | test_lexer/compiler/vm/gc/host/perf/corpus.cpp 분리 |
   | G.2 | check 단계 강화 | ✅ 완료 | trait 불일치 진단, 시그니처 오류, import/export 검사 |
   | G.3 | 오류 메시지 개선 | ✅ 완료 | 스택 트레이스, exhaustiveness 힌트, trait method 안내 |
   | G.4 | Bytecode Dump 연동 | ✅ 완료 | superinstruction 표시, dump 회귀 테스트 |
   | G.5 | Corpus 기반 회귀 테스트 | ✅ 완료 | tests/corpus/*.zph 6개 스크립트 추가 |
   ```

## 주의사항
- 각 태스크 완료 후 반드시 빌드 + 테스트 실행
- 기존 벤치마크 게이트(5/5 PASS) 유지 필수
- G.1 분리 작업은 마지막에 진행 (나머지 테스트가 먼저 안정화된 후)
