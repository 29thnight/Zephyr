# Wave H — 플랫폼 & 확장 구현 지시

## 참고
- H.0 Register-based VM: 전체 VM/컴파일러 재설계 필요 → 별도 브랜치로 진행 (이 세션에서 제외)
- H.1 CMake 크로스플랫폼: 보류 (이 세션에서 제외)

## 진행 순서
H.5 문자열 인터닝 → H.3 표준 라이브러리 → H.4 패키지 모델 → H.2 .inl→.cpp 분리

## 빌드 및 테스트 명령 (각 태스크 완료 후 반드시 실행)
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## H.5 문자열 인터닝 `[S]`

동일 문자열 값을 단일 StringObject로 공유해 반복 문자열 비교/GC 비용을 절감한다.

### 구현 내용

1. **인터닝 테이블 추가** (`src/zephyr_gc.inl` 또는 `src/zephyr_compiler.inl`):
   ```cpp
   std::unordered_map<std::string, StringObject*> string_intern_table_;
   ```
   - GC root로 등록 (GC trace 시 순회)

2. **intern_string() 함수 추가**:
   ```cpp
   StringObject* intern_string(const std::string& s);
   // 테이블에 있으면 기존 StringObject* 반환
   // 없으면 새 StringObject 할당 후 테이블에 등록 후 반환
   ```

3. **문자열 생성 경로에 인터닝 적용**:
   - 문자열 리터럴 컴파일 시 → `intern_string()` 사용
   - 짧은 문자열 (≤64자)만 인터닝 (긴 문자열은 동적 생성이 많아 효과 적음)
   - 동적 연결(concat), f-string 결과는 인터닝하지 않음 (매번 다른 값)

4. **StringObject 비교 최적화**:
   - 인터닝된 문자열끼리는 포인터 비교로 동치 확인 가능
   - `is_interned` 플래그 추가 (optional, 안전한 경우만)

5. **GC 처리**:
   - GC sweep 시 인터닝 테이블에서 수집된 StringObject 제거
   - 약한 참조 방식: StringObject가 GC되면 테이블에서도 제거

6. **api.hpp에 통계 추가**:
   - `ZephyrRuntimeStats`에 `string_intern_hits`, `string_intern_misses` 필드 추가

7. **tests/test_gc.cpp 또는 tests/test_vm.cpp에 테스트 추가**:
   - 동일 문자열 리터럴 두 번 사용 시 같은 포인터인지 확인
   - 인터닝 통계 확인

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 게이트 유지

---

## H.3 표준 라이브러리 기초 `[M]`

`std/math.zph`, `std/string.zph`, `std/collections.zph` 기초 구성.

### 구현 내용

1. **std 디렉토리 생성**: `std/`

2. **std/math.zph** — 수학 유틸리티:
   ```
   // 수학 함수들 - 호스트 바인딩으로 구현
   export fn abs(x: Int) -> Int { ... }
   export fn min(a: Int, b: Int) -> Int { if a < b { a } else { b } }
   export fn max(a: Int, b: Int) -> Int { if a > b { a } else { b } }
   export fn clamp(v: Int, lo: Int, hi: Int) -> Int { min(max(v, lo), hi) }
   export fn pow(base: Int, exp: Int) -> Int { ... }
   ```
   - 삼각함수, sqrt 등은 호스트 함수로 등록 필요 → 현재는 순수 Zephyr로 구현 가능한 것만

3. **std/string.zph** — 문자열 유틸리티:
   ```
   export fn starts_with(s: String, prefix: String) -> Bool { ... }
   export fn ends_with(s: String, suffix: String) -> Bool { ... }
   export fn repeat(s: String, n: Int) -> String { ... }
   export fn trim(s: String) -> String { ... }  // 호스트 함수 필요 시 스킵
   ```

4. **std/collections.zph** — 컬렉션 유틸리티:
   ```
   export fn range(start: Int, end: Int) -> [Int] { ... }  // 배열 생성
   export fn map_array(arr: [Any], f: fn(Any) -> Any) -> [Any] { ... }
   export fn filter_array(arr: [Any], pred: fn(Any) -> Bool) -> [Any] { ... }
   export fn fold_array(arr: [Any], init: Any, f: fn(Any, Any) -> Any) -> Any { ... }
   ```
   - Zephyr 언어로 구현 가능한 것만 포함 (호스트 없이 동작)

5. **모듈 검색 경로 설정 API** (`include/zephyr/api.hpp`):
   ```cpp
   void add_module_search_path(const std::string& path);
   // import "std/math" 시 검색 경로에서 std/math.zph 찾기
   ```

6. **기본 검색 경로에 실행 파일 디렉토리 추가**:
   - `zephyr_cli` 실행 시 현재 디렉토리 + 실행 파일 위치 기본 포함

7. **tests/test_vm.cpp 또는 tests/test_corpus.cpp에 테스트 추가**:
   - `import "std/math"` 후 `min`, `max`, `clamp` 동작 확인
   - `import "std/collections"` 후 `range`, `map_array` 동작 확인

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## H.4 패키지 모델 기초 `[M]`

패키지 레이아웃 규약을 정의하고 모듈 검색 경로 설정 API를 추가한다.

### 구현 내용

1. **패키지 레이아웃 규약 문서** (`docs/package_layout.md`):
   ```
   my_package/
   ├── package.toml       # 패키지 메타데이터
   ├── src/
   │   ├── lib.zph        # 진입점 (export 모음)
   │   └── *.zph          # 내부 모듈
   └── tests/
       └── *.zph          # 패키지 테스트
   ```

2. **package.toml 형식 정의** (문서화만, 파싱 구현은 선택):
   ```toml
   [package]
   name = "my_package"
   version = "0.1.0"
   entry = "src/lib.zph"

   [dependencies]
   # 향후 확장
   ```

3. **모듈 검색 경로 API 구현** (H.3에서 추가한 `add_module_search_path` 확장):
   ```cpp
   // api.hpp 추가
   std::vector<std::string> get_module_search_paths() const;
   void clear_module_search_paths();
   void set_package_root(const std::string& path);
   // set_package_root → package.toml 읽어서 entry, search paths 설정
   ```

4. **package.toml 파서 (최소 구현)**:
   - `std::ifstream`으로 읽어서 `[package]` 섹션의 `name`, `version`, `entry` 파싱
   - 본격적인 TOML 파서 불필요 — 줄 단위 파싱으로 충분
   - 파싱 실패 시 default 값 사용

5. **tests에 패키지 테스트 추가**:
   - 임시 디렉토리에 package.toml + src/lib.zph 생성
   - `set_package_root()` 호출 후 `import "lib"` 정상 작동 확인

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## H.2 .inl → .cpp 완전 분리 `[L]` `[Risk: Medium]`

> ⚠️ 가장 마지막에 진행. 빌드 구조 변경이므로 단계별로 신중하게 진행.

현재 구조: `src/zephyr.cpp`가 모든 `.inl` 파일을 `#include`하는 단일 컴파일 유닛.
목표: 각 `.inl`을 독립 `.cpp`로 전환해 수정 시 전체 재빌드 방지.

### 현재 구조 파악 (먼저 확인)
- `src/zephyr.cpp` 내용 읽기
- 각 `.inl` 파일의 의존성 파악

### 구현 전략

**Option A (권장 - 안전)**: `.inl` 파일을 `.cpp`로 복사/이름변경하고 forward declaration 헤더 추가
1. 각 `.inl`의 공개 선언을 헤더 파일로 추출
2. `.inl` → `.cpp` 이름 변경 (또는 새 `.cpp` 생성)
3. `zephyr.cpp`에서 해당 include 제거
4. 각 `.cpp`에 필요한 헤더 추가
5. `ZephyrRuntime.vcxproj`에 새 `.cpp` 파일 추가

**Option B (대안 - 최소 변경)**: 단일 컴파일 유닛 유지하되, 자주 변경되는 파일만 분리
- `src/zephyr_gc.inl` (가장 큰 파일) → `src/zephyr_gc.cpp`로만 분리
- 나머지는 현재 구조 유지

### 권장 접근
Option B로 시작: `zephyr_gc.inl`만 독립 `.cpp`로 분리 후 빌드 성공 확인.
성공하면 추가로 `zephyr_compiler.inl`도 분리 시도.

### 구현 단계
1. `src/zephyr.cpp`와 `.inl` 파일들의 include 관계 파악
2. `zephyr_gc.inl` → `zephyr_gc.cpp`로 분리:
   - 필요한 forward declaration 헤더 (`zephyr_gc.hpp`) 생성
   - `zephyr.cpp`에서 `#include "zephyr_gc.inl"` 제거
   - `ZephyrRuntime.vcxproj`에 `zephyr_gc.cpp` 추가
3. 빌드 성공 확인
4. 성공 시 `zephyr_compiler.inl`도 동일하게 분리

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 게이트 유지

---

## 완료 후 처리

1. **벤치마크 실행**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   ```

2. **벤치마크 저장**:
   ```powershell
   x64\Release\zephyr_bench.exe --output bench\results\wave_h_baseline.json
   ```

3. **process.md 업데이트** — Wave H 항목을 ✅ 완료로 변경:
   ```
   | H.2 | .inl → .cpp 완전 분리 | ✅ 완료 | zephyr_gc.cpp + (zephyr_compiler.cpp) 분리 |
   | H.3 | 표준 라이브러리 기초 | ✅ 완료 | std/math.zph, std/string.zph, std/collections.zph |
   | H.4 | 패키지 모델 기초 | ✅ 완료 | package.toml, set_package_root(), 검색 경로 API |
   | H.5 | 문자열 인터닝 | ✅ 완료 | intern_string(), GC root, 포인터 비교 최적화 |
   ```

## 주의사항
- H.2는 반드시 마지막에 진행
- H.2 실패 시 되돌리고 Option B 최소 변경으로 재시도
- 각 태스크 완료 후 빌드 + 테스트 필수
- 벤치마크 5/5 PASS 유지 필수
