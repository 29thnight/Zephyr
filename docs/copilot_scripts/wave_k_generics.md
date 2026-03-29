# Wave K: 제네릭(Generics) 구현

## 브랜치
현재 브랜치: `master`

## 목표
Zephyr에 제네릭 타입 파라미터를 추가한다.

지원할 문법:
```
// 제네릭 함수
fn identity<T>(x: T) -> T { x }
fn first<A, B>(pair: Pair<A, B>) -> A { pair.first }
fn map<T, U>(arr: [T], f: fn(T) -> U) -> [U] { ... }

// 제네릭 구조체
struct Pair<A, B> {
    first: A,
    second: B,
}

// 제네릭 trait bound
fn print_all<T: Display>(items: [T]) { ... }

// 제네릭 impl 블록
impl<T> Container<T> {
    fn push(self, item: T) { ... }
    fn get(self, idx: int) -> T { ... }
}

// 제네릭 함수 호출 (이미 파서 지원)
let p = Pair<int, string> { first: 1, second: "hello" };
let x = identity<int>(42);
```

## 구현 접근: 타입 소거 (Type Erasure)
Zephyr는 동적 타입 언어다. 제네릭 파라미터는 런타임에 모두 `Value`로 소거된다.
- 제네릭은 컴파일 타임 문법 + `check` 단계 타입 힌트로만 작동
- 별도 monomorphization 없음, 단일 bytecode 생성
- `fn identity<T>(x: T)` → 런타임엔 그냥 `fn identity(x)` 와 동일

## 빌드 및 테스트
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## Step 0: 현재 코드 파악

다음 파일들을 읽어 현재 구조를 이해하라.

### 0-1. AST 노드 파악
`src/zephyr_parser.inl` 에서 다음을 찾아 읽어라:
- `FunctionDecl` struct 정의 (필드 목록)
- `StructDecl` struct 정의 (필드 목록)
- `TraitDecl` struct 정의
- `ImplDecl` struct 정의
- `parse_function_decl()` 구현 전체
- `parse_struct_decl()` 구현 전체
- `parse_impl_decl()` 구현 전체

```powershell
$lines = Get-Content "src\zephyr_parser.inl"
# FunctionDecl, StructDecl 등 struct 정의 찾기
$lines | Select-String "struct FunctionDecl|struct StructDecl|struct TraitDecl|struct ImplDecl" | ForEach-Object { "Line $($_.LineNumber): $($_.Line)" }
```

### 0-2. 컴파일러 파악
`src/zephyr_compiler.inl` 에서 다음을 읽어라:
- `compile_bytecode_function()` — generic_params 처리 방식
- `register_struct_decl()` 또는 StructDecl 컴파일 코드 (line 5175 근처)
- `register_impl_decl()` 구현

```powershell
$lines = Get-Content "src\zephyr_compiler.inl"
$lines | Select-String "StructDecl\*|register_struct|compile_struct|5175" | Select-Object -First 20
```

---

## Step 1: 파서 — 제네릭 타입 파라미터 선언 파싱

### 1-1. AST 노드에 generic_params 추가

`FunctionDecl`에 이미 generic_params가 있는지 확인하라.
없으면 추가:

```cpp
struct FunctionDecl : Stmt {
    std::string name;
    std::vector<std::string> generic_params;  // <T, U, ...> — 추가
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockStmt> body;
    bool is_coroutine = false;
};
```

`StructDecl`에 추가:
```cpp
struct StructDecl : Stmt {
    std::string name;
    std::vector<std::string> generic_params;  // <T, U, ...> — 추가
    // ... 기존 필드들
};
```

`TraitDecl`에 추가:
```cpp
struct TraitDecl : Stmt {
    std::string name;
    std::vector<std::string> generic_params;  // <T> — 추가
    // ...
};
```

`ImplDecl`에 추가:
```cpp
struct ImplDecl : Stmt {
    std::vector<std::string> generic_params;  // <T> in impl<T> — 추가
    // ...
};
```

### 1-2. 파서 헬퍼 함수 추가

`Parser` 클래스에 헬퍼 추가:

```cpp
// src/zephyr_parser.inl 의 Parser 클래스 private 섹션에 추가
VoidResult parse_generic_type_params(std::vector<std::string>& out_params);
```

구현:
```cpp
VoidResult Parser::parse_generic_type_params(std::vector<std::string>& out_params) {
    // '<' 확인
    if (!check(TokenType::Less)) return {};
    advance();  // consume '<'

    // 첫 번째 타입 파라미터
    if (!check(TokenType::Identifier)) {
        return make_error("Expected type parameter name", current_span());
    }
    out_params.push_back(current().value);
    advance();

    // 추가 타입 파라미터 (콤마로 구분)
    while (check(TokenType::Comma)) {
        advance();  // consume ','
        if (!check(TokenType::Identifier)) {
            return make_error("Expected type parameter name after ','", current_span());
        }
        out_params.push_back(current().value);
        advance();
    }

    // '>' 닫기
    if (!check(TokenType::Greater)) {
        return make_error("Expected '>' after type parameters", current_span());
    }
    advance();  // consume '>'
    return {};
}
```

> **주의**: `TokenType::Less`와 `TokenType::Greater`가 실제 토큰 이름인지 확인하라.
> `zephyr_lexer.inl`에서 `<`와 `>`의 TokenType 이름을 찾아서 맞춰라.

### 1-3. parse_function_decl() 수정

`parse_function_decl()` 에서 함수 이름 파싱 직후 `<T>` 파싱 추가:

```cpp
RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_function_decl() {
    // ... 기존 코드: 'fn' keyword 소비, 이름 파싱 ...
    auto function = std::make_unique<FunctionDecl>();
    function->name = /* 현재 identifier */;
    advance();

    // 제네릭 파라미터 파싱 추가 ↓
    ZEPHYR_TRY(parse_generic_type_params(function->generic_params));

    ZEPHYR_TRY(parse_function_signature(function->params, function->return_type));
    // ... 나머지 기존 코드 ...
}
```

### 1-4. parse_struct_decl() 수정

```cpp
RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_struct_decl() {
    // ... 기존 코드: 'struct' keyword 소비, 이름 파싱 ...
    auto decl = std::make_unique<StructDecl>();
    decl->name = /* 현재 identifier */;
    advance();

    // 제네릭 파라미터 파싱 추가 ↓
    ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));

    // ... 나머지 기존 코드 ('{' 파싱 등) ...
}
```

### 1-5. parse_impl_decl() 수정

`impl<T> StructName<T>` 형태 지원:

```cpp
RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_impl_decl() {
    // 'impl' keyword 소비
    advance();

    auto decl = std::make_unique<ImplDecl>();

    // impl<T> 파싱 추가 ↓
    ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));

    // ... 나머지 기존 코드 (struct name, trait name 파싱 등) ...
}
```

### 1-6. parse_trait_decl() 수정 (선택)

```cpp
// 'trait' keyword 소비 후, trait 이름 파싱 후:
ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));
```

---

## Step 2: 컴파일러 — generic_params 전달

### 2-1. FunctionDecl 컴파일에서 generic_params 전달

`src/zephyr_compiler.inl` 에서 FunctionDecl을 컴파일하는 코드를 찾아라.
`compile_bytecode_function()` 호출 시 `function_decl->generic_params` 전달:

```cpp
// FunctionDecl 컴파일 코드에서:
auto bytecode = compile_bytecode_function(
    decl->name,
    decl->params,
    decl->body.get(),
    decl->generic_params  // ← 추가
);
```

이미 `compile_bytecode_function()`의 시그니처가 `generic_params` 파라미터를 받으므로 연결만 하면 됨.

### 2-2. StructDecl 컴파일에서 generic_params 저장

`StructTypeObject`에 `generic_params` 필드 추가:

```cpp
// src/zephyr_types.inl 또는 컴파일러 내 StructTypeObject 정의 근처:
struct StructTypeObject final : GcObject {
    explicit StructTypeObject(std::string name) : ...
    std::string name;
    std::vector<std::string> generic_params;  // ← 추가
    // ... 기존 필드들 ...
};
```

StructDecl 컴파일 시 generic_params 저장:
```cpp
// register_struct_decl() 또는 StructDecl 처리 코드에서:
auto* struct_type = /* 새 StructTypeObject 생성 */;
struct_type->generic_params = struct_decl->generic_params;
```

### 2-3. 제네릭 구조체 인스턴스화 처리

`Pair<int, string> { first: 1, second: "hello" }` 구문에서
현재 파서가 `Pair<int, string>`을 어떻게 처리하는지 확인하라:

```powershell
# struct literal 파싱 코드 찾기
$lines = Get-Content "src\zephyr_parser.inl"
$lines | Select-String "struct.*literal|StructLiteral|parse_struct_literal|StructInstantiation" | Select-Object -First 10
```

이미 `lookahead_generic_call_type_arguments()`로 `Foo<T>` 호출은 처리되므로,
구조체 리터럴에서도 동일하게 타입 인수가 허용되는지 확인.

허용되지 않는다면, 구조체 타입 참조에서 `<...>` 부분을 무시(타입 소거)하도록 처리:
```cpp
// TypeRef 파싱 시 Foo<T, U> 형태에서 <T, U>는 컬렉션 정보로만 저장
struct TypeRef {
    std::vector<std::string> parts;
    std::vector<TypeRef> type_args;  // 제네릭 타입 인수 ← 추가
    Span span;
};
```

---

## Step 3: trait bound 파싱 (선택적 구현)

`fn foo<T: Display>(x: T)` 형태의 trait bound 지원.

### 3-1. GenericParam 타입 추가

현재 `generic_params: vector<string>`을 `vector<GenericParam>`으로 확장:

```cpp
struct GenericParam {
    std::string name;                  // "T"
    std::vector<std::string> bounds;   // ["Display", "Clone"]
};
```

`parse_generic_type_params()` 수정:
```cpp
// "T: Bound1 + Bound2" 파싱
out_params.push_back(current().value);  // T
advance();
// ':' 다음에 trait bound 파싱
if (check(TokenType::Colon)) {
    advance();
    // bound 이름 파싱 (+ 로 구분)
    // ...
}
```

> 이 Step은 복잡도가 높다. 구현이 어려우면 skip하고 Step 4로 이동.

---

## Step 4: 테스트 추가

### 4-1. 파서 테스트

`tests/test_compiler.cpp` 에 제네릭 관련 테스트 추가:

```cpp
// 제네릭 함수 파싱 테스트
TEST(Compiler, GenericFunction) {
    auto result = vm.check_string(R"(
        fn identity<T>(x: T) -> T { x }
        let r = identity<int>(42);
    )");
    EXPECT_TRUE(result.ok());
}

// 제네릭 구조체 파싱 테스트
TEST(Compiler, GenericStruct) {
    auto result = vm.check_string(R"(
        struct Pair<A, B> {
            first: A,
            second: B,
        }
        let p = Pair { first: 1, second: "hello" };
    )");
    EXPECT_TRUE(result.ok());
}

// 제네릭 함수 실행 테스트
TEST(Compiler, GenericFunctionExecution) {
    auto result = vm.execute_string(R"(
        fn identity<T>(x: T) -> T { x }
        fn swap<A, B>(a: A, b: B) -> B { b }
        identity<int>(42)
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "42");
}

// 제네릭 구조체 인스턴스화
TEST(Compiler, GenericStructInstantiation) {
    auto result = vm.execute_string(R"(
        struct Pair<A, B> {
            first: A,
            second: B,
        }
        let p = Pair { first: 10, second: "world" };
        p.first
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "10");
}
```

### 4-2. corpus 테스트 스크립트 추가

`tests/corpus/generics.zph` 파일 생성:
```
fn identity<T>(x: T) -> T { x }

fn first<A, B>(a: A, b: B) -> A { a }

struct Pair<A, B> {
    fst: A,
    snd: B,
}

fn main() -> int {
    let x = identity<int>(42);
    let s = identity<string>("hello");
    let p = Pair { fst: 1, snd: "two" };
    let f = first<int, string>(10, "ignored");
    x
}
```

---

## 빌드 및 검증

```powershell
# 빌드
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal

# 테스트
x64\Release\zephyr_tests.exe

# 문법 확인
x64\Release\zephyr_cli.exe check tests\corpus\generics.zph
x64\Release\zephyr_cli.exe run tests\corpus\generics.zph
```

---

## 주의사항

1. **TokenType 이름 확인**: `<` `>` 의 실제 TokenType을 `src/zephyr_lexer.inl`에서 확인 후 사용
2. **lookahead 충돌**: `fn foo<T>()` 에서 `<T>` 가 비교 연산(`foo < T > bar`)과 구별돼야 함
   - 선언부: `fn` keyword 직후 이름 다음의 `<`는 항상 type params로 파싱
   - 호출부: 기존 `lookahead_generic_call_type_arguments()` 로직 유지
3. **타입 소거 원칙**: 런타임에는 모든 `T`가 그냥 `Value` — 별도 코드 생성 불필요
4. **backward compatibility**: 기존 `generic_params = {}` 기본값 유지로 하위 호환 보장
5. 각 Step 완료 후 `zephyr_tests.exe` 통과 필수

## 커밋 메시지
```
feat: generic type parameters (Wave K)

- Parser: fn<T>, struct<T>, impl<T>, trait<T> declaration syntax
- Compiler: forward generic_params from AST to BytecodeFunction
- Type erasure: generics are compile-time only, no runtime overhead
- Tests: generic function, struct, corpus script
```
