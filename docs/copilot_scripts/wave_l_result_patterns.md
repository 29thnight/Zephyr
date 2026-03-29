# Wave L: Result 타입 + 패턴 매칭 강화

## 브랜치
현재 브랜치: `master`

## 목표

### Part A: Result 타입
```zephyr
// Ok/Err 생성 (이미 부분 동작)
let ok_val = Ok(42);
let err_val = Err("not found");

// match로 처리
let result = Ok(10);
let x = match result {
    Ok(v)  => v * 2,
    Err(e) => 0,
};

// ? 연산자 - 함수 내에서 Err이면 즉시 return Err(...)
fn divide(a: int, b: int) -> result<int, string> {
    if b == 0 {
        return Err("division by zero");
    }
    Ok(a / b)
}

fn compute() -> result<int, string> {
    let x = divide(10, 2)?;   // Err면 즉시 return
    let y = divide(x, 3)?;
    Ok(y)
}
```

### Part B: 패턴 매칭 강화
```zephyr
// 배열 패턴 — head/tail
let arr = [1, 2, 3, 4];
match arr {
    []               => "empty",
    [x]              => "one element",
    [first, ..rest]  => f"starts with {first}",
}

// 배열 패턴 — 앞/뒤 고정
match arr {
    [a, b, ..] => f"first two: {a}, {b}",
    _          => "other",
}

// 구조체 필드 destructuring 패턴
struct Point { x: int, y: int }
let p = Point { x: 3, y: 4 };
match p {
    Point { x: 0, y }  => f"on y-axis at {y}",
    Point { x, y: 0 }  => f"on x-axis at {x}",
    Point { x, y }     => f"at ({x}, {y})",
}

// 중첩 패턴
match result {
    Ok(Point { x, y }) => f"ok point ({x},{y})",
    Err(msg)           => f"error: {msg}",
}

// 범위 패턴 (선택)
match score {
    0       => "zero",
    1..=59  => "fail",
    60..=79 => "pass",
    _       => "excellent",
}
```

## 빌드 및 테스트
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## Step 0: 현재 상태 파악

### 0-1. 현재 Result 지원 범위 확인
```powershell
Set-Location "C:\Users\lance\OneDrive\Documents\Project Zephyr"
# Result enum 정의 위치 확인
Select-String -Path "src\zephyr_compiler.inl" -Pattern "Result.*Ok|Ok.*Result|register.*Result|builtin_result|\"Ok\"|\"Err\"" | Select-Object -First 20
# ? 연산자 관련 토큰 확인
Select-String -Path "src\zephyr_lexer.inl" -Pattern "Question\b|QuestionMark" | Select-Object -First 10
```

### 0-2. 현재 패턴 종류 파악
```powershell
# parse_pattern_primary 전체 읽기
$lines = Get-Content "src\zephyr_parser.inl"
# 패턴 파싱 시작 라인 찾기
$start = ($lines | Select-String "parse_pattern_primary\(\)").LineNumber[0]
$lines[($start-1)..($start+80)] -join "`n"
```

```powershell
# compile_pattern 전체 읽기
$lines = Get-Content "src\zephyr_compiler.inl"
$start = ($lines | Select-String "void compile_pattern").LineNumber[0]
$lines[($start-1)..($start+100)] -join "`n"
```

### 0-3. Result 테스트 - 현재 동작 확인
```powershell
# 간단한 Result 테스트 스크립트 실행
@"
let r = Ok(42);
let v = match r {
    Ok(x) => x,
    Err(e) => 0,
};
v
"@ | Set-Content test_result_check.zph
.\x64\Release\zephyr_cli.exe run test_result_check.zph
Remove-Item test_result_check.zph
```

---

## Part A: Result 타입 완성

### A-1. Result 내장 enum 등록 확인

`src/zephyr_compiler.inl` 또는 `src/zephyr_gc.inl`에서 Result 열거형이 어떻게 등록되는지 확인하라.

만약 Result가 명시적으로 등록된 enum이 아닌 경우, 런타임 초기화 시 다음을 추가:

```cpp
// Runtime 초기화 (initialize() 또는 setup_builtins() 함수 내)
// Result 내장 enum 등록
{
    auto result_type = allocate<EnumTypeObject>("Result");
    result_type->variants.push_back({"Ok",  1});  // Ok(value)
    result_type->variants.push_back({"Err", 1});  // Err(error)
    // root environment에 "Result" 바인딩
    root_environment_->define("Result", Value::object(result_type));
}
```

### A-2. Ok(v) / Err(e) 생성 문법 확인

현재 `Ok(value)`, `Err(error)` 가 동작하는지 테스트해라 (Step 0-3에서 확인).
동작하지 않는다면:

컴파일러에서 함수 호출 시 특수 처리 추가:
```cpp
// 함수 호출 컴파일 시, callee가 "Ok" 또는 "Err"이면 enum 생성으로 처리
if (callee_name == "Ok" || callee_name == "Err") {
    // build_enum_value("Result", callee_name, args) 호출
}
```

또는 `Ok`/`Err`를 네이티브 함수로 등록:
```cpp
register_global_function("Ok", [](std::vector<Value> args) -> Value {
    // build Result::Ok(args[0])
});
register_global_function("Err", [](std::vector<Value> args) -> Value {
    // build Result::Err(args[0])
});
```

### A-3. ? 연산자 구현

#### A-3-1. 렉서에 `?` 토큰 추가

`src/zephyr_lexer.inl`에서:
- `TokenType` enum에 `Question` 추가 (기존 `QuestionDot`과 구분)
- 렉서 스캔: `?` 다음이 `.`이면 `QuestionDot`, 아니면 `Question`

```cpp
// 렉서의 스캔 로직에서 '?' 처리:
case '?':
    if (peek() == '.') {
        advance();
        return make_token(TokenType::QuestionDot);
    }
    return make_token(TokenType::Question);
```

#### A-3-2. 파서에 후위 `?` 연산자 추가

`parse_call()` 또는 후위 연산자 처리 위치에서:

```cpp
// parse_call() 루프 내에서 후위 ? 처리
if (check(TokenType::Question)) {
    advance();
    auto try_expr = std::make_unique<TryExpr>();
    try_expr->span = current_span();
    try_expr->inner = std::move(expr);
    expr = std::move(try_expr);
}
```

`TryExpr` AST 노드 추가:
```cpp
struct TryExpr : Expr {
    std::unique_ptr<Expr> inner;
    // ? 연산자: inner가 Err이면 즉시 return Err(e)
};
```

#### A-3-3. 컴파일러에 TryExpr 처리 추가

```cpp
// compile_expr에서 TryExpr 처리:
if (auto* try_expr = dynamic_cast<TryExpr*>(expr)) {
    // inner 식 컴파일 (result를 스택/레지스터에 남김)
    compile_expr(try_expr->inner.get());
    // result를 임시 변수에 저장
    std::string tmp = gensym("try_result");
    emit_store_local(tmp);

    // if IsEnumVariant(tmp, "Result", "Err") => return Err(payload)
    emit(BytecodeOp::IsEnumVariant, span, 1, "Result", "Err");
    int ok_jump = emit_jump_if_false();

    // Err 경로: payload 추출 후 return Err(payload)
    emit_load_local(tmp);
    emit(BytecodeOp::GetEnumPayload, span, 0);  // payload[0]
    // build_enum_value("Result", "Err", [payload])
    emit(BytecodeOp::BuildEnum, span, /* Result, Err */);
    emit(BytecodeOp::Return, span, 0);

    // Ok 경로: payload 추출해서 값으로 사용
    patch_jump(ok_jump);
    emit_load_local(tmp);
    emit(BytecodeOp::GetEnumPayload, span, 0);  // payload[0] = Ok의 값
    return;
}
```

> 구체적인 opcode 이름은 기존 코드에서 사용하는 것을 확인해서 맞춰라.
> `GetEnumPayload`는 `get_enum_payload_value()` 런타임 함수에 대응하는 opcode일 것.

---

## Part B: 패턴 매칭 강화

### B-1. 현재 패턴 종류 파악 (Step 0에서 확인)

현재 지원되는 패턴 종류를 확인하라:
- 리터럴 패턴: `42`, `"hello"`, `true`
- 와일드카드: `_`
- 바인딩: `x`
- OR 패턴: `A | B`
- Enum 패턴: `Ok(x)`, `Some(v, w)`
- Guard: `x if x > 0`

### B-2. 배열 패턴 추가

#### B-2-1. ArrayPattern AST 노드 추가

```cpp
// Pattern 타입 목록에 추가
struct ArrayPattern : Pattern {
    std::vector<PatternPtr> elements;   // 앞쪽 고정 패턴들
    std::optional<std::string> rest;    // ..rest 바인딩 이름 (없으면 nullopt, "_"이면 무시)
    bool has_rest = false;              // .. 가 있으면 true
};
```

#### B-2-2. parse_pattern_primary()에 배열 패턴 파싱 추가

`parse_pattern_primary()`에서 `[` 토큰 처리 추가:

```cpp
// '[' 로 시작하면 배열 패턴
if (check(TokenType::LeftBracket)) {
    advance();  // consume '['
    auto array_pat = std::make_unique<ArrayPattern>();

    while (!check(TokenType::RightBracket) && !is_at_end()) {
        // ".." 처리 (rest pattern)
        if (check(TokenType::DotDot)) {
            advance();
            array_pat->has_rest = true;
            // 다음이 identifier면 rest 바인딩 이름
            if (check(TokenType::Identifier)) {
                array_pat->rest = current().value;
                advance();
            }
            break;  // .. 이후 더 이상 패턴 없음
        }
        ZEPHYR_TRY_ASSIGN(elem_pat, parse_pattern());
        array_pat->elements.push_back(std::move(elem_pat));
        if (!check(TokenType::RightBracket)) {
            ZEPHYR_TRY(expect(TokenType::Comma, "Expected ',' in array pattern"));
        }
    }
    ZEPHYR_TRY(expect(TokenType::RightBracket, "Expected ']' to close array pattern"));
    return array_pat;
}
```

> `TokenType::DotDot` 이 없으면 `..` 를 두 개의 `.`으로 파싱하는지 확인.
> 없으면 렉서에 `DotDot` 토큰 추가 필요.

#### B-2-3. compile_pattern()에 배열 패턴 컴파일 추가

```cpp
if (auto* arr_pat = dynamic_cast<ArrayPattern*>(pattern)) {
    // 1. subject가 배열인지 확인
    emit_load_local(subject_name);
    emit(BytecodeOp::IsArray, span, 0);
    failure_jumps.push_back(emit_jump_if_false());

    // 2. 길이 체크
    if (!arr_pat->has_rest) {
        // 정확히 elements.size() 개수
        emit_load_local(subject_name);
        emit(BytecodeOp::ArrayLen, span, 0);
        emit_const(static_cast<int>(arr_pat->elements.size()));
        emit(BytecodeOp::CmpEq, span, 0);
        failure_jumps.push_back(emit_jump_if_false());
    } else {
        // 최소 elements.size() 개수
        emit_load_local(subject_name);
        emit(BytecodeOp::ArrayLen, span, 0);
        emit_const(static_cast<int>(arr_pat->elements.size()));
        emit(BytecodeOp::CmpGe, span, 0);
        failure_jumps.push_back(emit_jump_if_false());
    }

    // 3. 각 원소 패턴 재귀 컴파일
    for (size_t i = 0; i < arr_pat->elements.size(); ++i) {
        std::string elem_name = gensym("arr_elem");
        emit_load_local(subject_name);
        emit_const(static_cast<int>(i));
        emit(BytecodeOp::Index, span, 0);  // arr[i]
        emit_store_local(elem_name);
        compile_pattern(elem_name, arr_pat->elements[i].get(), failure_jumps);
    }

    // 4. rest 바인딩 처리
    if (arr_pat->has_rest && arr_pat->rest.has_value() && *arr_pat->rest != "_") {
        // subject[elements.size()..] 를 rest 바인딩에 저장
        emit_load_local(subject_name);
        emit_const(static_cast<int>(arr_pat->elements.size()));
        emit(BytecodeOp::ArraySliceFrom, span, 0);  // arr[start..]
        emit_store_local(*arr_pat->rest);
    }
    return;
}
```

> 구체적인 opcode 이름(IsArray, ArrayLen, CmpGe, Index, ArraySliceFrom 등)은
> 기존 컴파일러에서 사용하는 opcode를 찾아서 맞춰라.
> 없는 opcode는 런타임에 네이티브 함수 호출로 대체 가능.

### B-3. 구조체 필드 destructuring 패턴 추가

#### B-3-1. StructPattern AST 노드 추가

```cpp
struct StructFieldPattern {
    std::string field_name;
    std::optional<PatternPtr> pattern;  // 없으면 field_name 자체가 바인딩
};

struct StructPattern : Pattern {
    std::string type_name;                       // "Point"
    std::vector<StructFieldPattern> fields;      // { x, y: 0 }
    bool has_rest = false;                       // { x, .. } 에서 ..
};
```

#### B-3-2. parse_pattern_primary()에 구조체 패턴 파싱 추가

Identifier 다음에 `{` 가 오면 구조체 패턴으로 파싱:

```cpp
// 기존 Identifier 처리에서
if (check(TokenType::Identifier)) {
    std::string name = current().value;
    advance();

    // Struct pattern: Name { field1, field2: pattern, .. }
    if (check(TokenType::LeftBrace)) {
        advance();  // consume '{'
        auto struct_pat = std::make_unique<StructPattern>();
        struct_pat->type_name = name;

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            if (check(TokenType::DotDot)) {
                advance();
                struct_pat->has_rest = true;
                break;
            }
            StructFieldPattern field;
            ZEPHYR_TRY(expect_identifier(field.field_name));
            if (check(TokenType::Colon)) {
                advance();
                ZEPHYR_TRY_ASSIGN(field.pattern, parse_pattern());
            }
            // else: field_name itself is the binding
            struct_pat->fields.push_back(std::move(field));
            if (!check(TokenType::RightBrace)) {
                ZEPHYR_TRY(expect(TokenType::Comma));
            }
        }
        ZEPHYR_TRY(expect(TokenType::RightBrace));
        return struct_pat;
    }
    // ...기존 enum/binding 처리...
}
```

#### B-3-3. compile_pattern()에 구조체 패턴 컴파일 추가

```cpp
if (auto* struct_pat = dynamic_cast<StructPattern*>(pattern)) {
    // 1. struct 타입 이름 확인
    emit_load_local(subject_name);
    emit(BytecodeOp::IsStructType, span, 0, struct_pat->type_name);
    failure_jumps.push_back(emit_jump_if_false());

    // 2. 각 필드 패턴 재귀 컴파일
    for (const auto& field : struct_pat->fields) {
        std::string field_val = gensym("struct_field");
        emit_load_local(subject_name);
        emit(BytecodeOp::GetField, span, 0, field.field_name);
        emit_store_local(field_val);

        if (field.pattern) {
            // field: pattern 형태
            compile_pattern(field_val, field.pattern->get(), failure_jumps);
        } else {
            // field 이름 자체가 바인딩
            emit_load_local(field_val);
            emit_store_local(field.field_name);
        }
    }
    return;
}
```

> `IsStructType` opcode가 없으면 런타임 함수로 처리:
> 대상 값이 StructInstanceObject이고 type->name == struct_pat->type_name인지 확인

---

## Step 4: 테스트 추가

### test_compiler.cpp에 추가

```cpp
// Result 테스트
TEST(Compiler, ResultOkErr) {
    auto result = vm.execute_string(R"(
        let r = Ok(42);
        match r {
            Ok(v) => v,
            Err(e) => 0,
        }
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "42");
}

TEST(Compiler, ResultQuestionOp) {
    auto result = vm.execute_string(R"(
        fn safe_div(a: int, b: int) -> result<int, string> {
            if b == 0 { return Err("zero"); }
            Ok(a / b)
        }
        fn compute() -> result<int, string> {
            let x = safe_div(10, 2)?;
            Ok(x)
        }
        match compute() {
            Ok(v)  => v,
            Err(_) => -1,
        }
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "5");
}

// 배열 패턴 테스트
TEST(Compiler, ArrayPattern) {
    auto result = vm.execute_string(R"(
        fn describe(arr: [int]) -> string {
            match arr {
                []            => "empty",
                [x]           => "one",
                [a, b, ..rest] => f"many: {a},{b}",
            }
        }
        describe([1, 2, 3])
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "many: 1,2");
}

// 구조체 destructuring 패턴 테스트
TEST(Compiler, StructPattern) {
    auto result = vm.execute_string(R"(
        struct Point { x: int, y: int }
        let p = Point { x: 3, y: 4 };
        match p {
            Point { x: 0, y } => 0,
            Point { x, y }    => x + y,
        }
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "7");
}

// 중첩 패턴 테스트
TEST(Compiler, NestedPattern) {
    auto result = vm.execute_string(R"(
        struct Point { x: int, y: int }
        let r = Ok(Point { x: 1, y: 2 });
        match r {
            Ok(Point { x, y }) => x + y,
            Err(_)             => 0,
        }
    )");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->to_string(), "3");
}
```

### corpus 스크립트 추가

`tests/corpus/08_result_patterns.zph`:
```zephyr
fn safe_div(a: int, b: int) -> result<int, string> {
    if b == 0 { return Err("division by zero"); }
    Ok(a / b)
}

fn compute(x: int, y: int) -> result<int, string> {
    let q = safe_div(x, y)?;
    Ok(q * 2)
}

struct Vec2 { x: int, y: int }

fn classify(v: Vec2) -> string {
    match v {
        Vec2 { x: 0, y: 0 } => "origin",
        Vec2 { x: 0, y }    => f"y-axis:{y}",
        Vec2 { x, y: 0 }    => f"x-axis:{x}",
        Vec2 { x, y }       => f"({x},{y})",
    }
}

fn head_tail(arr: [int]) -> string {
    match arr {
        []             => "empty",
        [x]            => f"one:{x}",
        [a, b, ..rest] => f"head:{a} next:{b} rest_len:{rest.len()}",
    }
}

fn main() -> int {
    let r1 = compute(10, 2);
    let v1 = match r1 { Ok(v) => v, Err(_) => -1 };

    let r2 = compute(5, 0);
    let v2 = match r2 { Ok(v) => v, Err(_) => -1 };

    let p = Vec2 { x: 3, y: 0 };
    let c = classify(p);

    let arr = [10, 20, 30, 40];
    let h = head_tail(arr);

    v1  // 10
}
```

---

## 주의사항

1. **? 연산자**: `?`는 반드시 `Result`를 반환하는 함수 내부에서만 유효. 그렇지 않으면 컴파일 에러.
2. **DotDot 토큰**: `..` 이 이미 렉서에 있는지 확인. 없으면 추가 필요.
3. **IsStructType**: StructInstanceObject 타입 이름 비교. GC 루트 환경의 struct type lookup.
4. **ArraySliceFrom**: 없으면 런타임에 배열 slice 네이티브 함수로 대체.
5. 각 Part(A, B) 독립적으로 빌드+테스트. Part A가 실패해도 Part B 진행 가능.
6. `zephyr_tests.exe` 전체 통과 필수.

## 커밋 메시지
```
feat: Result type + enhanced pattern matching (Wave L)

Part A: Result<T,E> with Ok/Err construction and ? propagation operator
Part B: Array patterns [a, ..rest], struct field destructuring Point{x,y},
        nested patterns Ok(Point{x,y})
Tests: unit tests + corpus 08_result_patterns.zph
```
