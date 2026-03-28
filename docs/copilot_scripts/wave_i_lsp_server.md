# Wave I — LSP 서버 구현 (zephyr_cli.exe lsp)

## 브랜치
`master` 직접 커밋

## 목표
`zephyr_cli.exe lsp` 서브커맨드로 stdio 기반 LSP 서버 구현.
VS Code 확장이 이 프로세스를 실행해 Language Server로 사용한다.

## 빌드 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

---

## 구현할 LSP 기능

| 기능 | 우선순위 | 설명 |
|------|---------|------|
| Diagnostics | ★★★ | 파일 열기/저장 시 파싱+컴파일 오류 표시 |
| Hover | ★★★ | 심볼 위에 커서 올리면 타입/시그니처 표시 |
| Completion | ★★☆ | 키워드 + 선언된 심볼 자동완성 |
| Go-to-Definition | ★★☆ | F12로 함수/변수 선언부 이동 |
| Document Symbols | ★☆☆ | 파일 내 함수/클래스 목록 (Outline 패널) |

---

## Step 1: cli/main.cpp에 lsp 서브커맨드 추가

### 1-1. `cli/main.cpp` 읽기
현재 `run`, `check`, `repl`, `stats`, `dump-bytecode`, `bench` 커맨드가 있다.

### 1-2. lsp 서브커맨드 등록
`print_usage()` 함수에 `lsp` 항목 추가:
```cpp
std::cout << "  lsp              Start LSP server (stdin/stdout)\n";
```

main()의 커맨드 dispatch에 추가:
```cpp
if (command == "lsp") {
    return run_lsp_server(argv[0]);
}
```

---

## Step 2: LSP 서버 핵심 구현

`cli/lsp_server.cpp` 파일 신규 생성 (또는 cli/main.cpp 하단에 추가).

### 2-1. JSON-RPC 메시지 읽기/쓰기

```cpp
// stdin에서 Content-Length 헤더 읽고 본문 파싱
std::string lsp_read_message() {
    std::string line;
    int content_length = 0;
    while (std::getline(std::cin, line)) {
        // "\r" 제거
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // 헤더 끝
        if (line.rfind("Content-Length:", 0) == 0) {
            content_length = std::stoi(line.substr(15));
        }
    }
    if (content_length == 0) return "";
    std::string body(content_length, '\0');
    std::cin.read(body.data(), content_length);
    return body;
}

// stdout으로 Content-Length + 본문 전송
void lsp_send_message(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}
```

### 2-2. JSON 파싱 헬퍼 (기존 DAP 패턴 유지 - 문자열 검색)

기존 `dap_dispatch_request`처럼 `find()`로 메서드명/필드 추출:
```cpp
// "method":"textDocument/didOpen" 추출
std::string lsp_extract_string(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = json.find('"', pos);
    return json.substr(pos, end - pos);
}

int lsp_extract_int(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return -1;
    pos += search.size();
    return std::stoi(json.substr(pos));
}
```

### 2-3. 서버 상태 구조체

```cpp
struct LspServer {
    zephyr::ZephyrVM vm;
    // uri → source 내용 캐시
    std::unordered_map<std::string, std::string> open_documents;
    int next_request_id = 1000;
};
```

### 2-4. 메인 루프

```cpp
int run_lsp_server(const char* exe_path) {
    // stderr로 로그 (stdout은 LSP 전용)
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    LspServer server;
    server.vm.configure_default_module_paths(exe_path);

    while (true) {
        const std::string msg = lsp_read_message();
        if (msg.empty()) break;
        lsp_dispatch(server, msg);
    }
    return 0;
}
```

---

## Step 3: LSP 메서드 구현

### 3-1. initialize / initialized

```cpp
if (method == "initialize") {
    const std::string response = R"({
        "jsonrpc":"2.0","id":)" + id_str + R"(,
        "result":{
            "capabilities":{
                "textDocumentSync":1,
                "hoverProvider":true,
                "completionProvider":{"triggerCharacters":["."]},
                "definitionProvider":true,
                "documentSymbolProvider":true
            },
            "serverInfo":{"name":"zephyr-lsp","version":"0.1.0"}
        }
    })";
    lsp_send_message(response);
}
if (method == "initialized") {
    // 클라이언트 준비 완료, 아무것도 안 해도 됨
}
```

### 3-2. textDocument/didOpen + textDocument/didChange → Diagnostics

파일 열기/변경 시 파싱 + 컴파일해서 오류를 `publishDiagnostics`로 전송:

```cpp
void lsp_publish_diagnostics(LspServer& server, const std::string& uri, const std::string& source) {
    // source를 zephyr runtime에 파싱
    auto parse_result = server.vm.parse_source(source, uri);

    std::ostringstream diags;
    diags << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << uri << "\",\"diagnostics\":[";

    if (!parse_result) {
        // 오류 있을 때
        const auto& err = parse_result.error();
        diags << "{\"range\":{\"start\":{\"line\":" << (err.line - 1) << ",\"character\":" << err.column
              << "},\"end\":{\"line\":" << (err.line - 1) << ",\"character\":" << (err.column + 1) << "}},"
              << "\"severity\":1,\"message\":\"" << lsp_escape_string(err.message) << "\"}";
    }

    diags << "]}}";
    lsp_send_message(diags.str());
}
```

`parse_source()`의 RuntimeError 구조에서 line/column/message를 추출한다.
`include/zephyr/api.hpp`에서 `RuntimeError` 또는 `RuntimeResult<T>` 구조를 확인해 필드명 맞출 것.

### 3-3. textDocument/hover

커서 위치의 토큰을 찾아 키워드/내장 함수면 설명 반환:

```cpp
// 간단 구현: 위치에서 단어 추출 → 알려진 심볼 테이블에서 조회
std::string lsp_hover_content(const std::string& source, int line, int character) {
    // 해당 줄의 커서 위치 단어 추출
    const std::string word = extract_word_at(source, line, character);

    // 내장 함수/타입 설명 테이블
    static const std::unordered_map<std::string, std::string> builtin_docs = {
        {"print", "print(value: Any) -> Nil\nConsole output"},
        {"len",   "len(s: String) -> Int\nString length"},
        {"fn",    "fn name(params) -> ReturnType { ... }\nFunction declaration"},
        {"let",   "let name = value\nVariable declaration"},
        {"yield", "yield value\nYield from coroutine"},
        // ... 더 추가
    };

    auto it = builtin_docs.find(word);
    if (it != builtin_docs.end()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```zephyr\\n" + it->second + "\\n```\"}}";
    }
    return "{}";
}
```

### 3-4. textDocument/completion

키워드 목록 + 열린 문서에서 추출한 함수/변수명 반환:

```cpp
std::string lsp_completion_items() {
    static const std::vector<std::string> keywords = {
        "fn", "let", "if", "else", "while", "for", "return", "yield",
        "true", "false", "nil", "import", "export", "trait", "impl",
        "match", "struct", "class", "Int", "Float", "String", "Bool"
    };

    std::ostringstream items;
    items << "[";
    for (size_t i = 0; i < keywords.size(); ++i) {
        if (i > 0) items << ",";
        items << "{\"label\":\"" << keywords[i] << "\",\"kind\":14}"; // kind 14 = Keyword
    }
    items << "]";
    return items.str();
}
```

### 3-5. textDocument/definition

`check_file` 로직을 활용해 심볼 선언 위치 반환.
간단 구현: 현재 문서에서 `fn word_name(` 패턴 검색:

```cpp
std::string lsp_find_definition(const std::string& source, const std::string& uri, const std::string& word) {
    std::istringstream ss(source);
    std::string line;
    int line_num = 0;
    while (std::getline(ss, line)) {
        const std::string pattern = "fn " + word + "(";
        const auto col = line.find(pattern);
        if (col != std::string::npos) {
            return "{\"uri\":\"" + uri + "\",\"range\":{\"start\":{\"line\":" + std::to_string(line_num)
                + ",\"character\":" + std::to_string(col) + "},\"end\":{\"line\":" + std::to_string(line_num)
                + ",\"character\":" + std::to_string(col + word.size()) + "}}}";
        }
        ++line_num;
    }
    return "null";
}
```

### 3-6. shutdown / exit

```cpp
if (method == "shutdown") {
    lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":null}");
}
if (method == "exit") {
    break; // 루프 종료
}
```

---

## Step 4: include/zephyr/api.hpp에 parse_source 공개 API 확인

`Runtime::parse_source()`가 이미 public이면 그대로 사용.
오류 타입 (`RuntimeError`)에서 line/column/message 접근 가능한지 확인:

```cpp
// api.hpp에 없으면 추가:
struct ParseError {
    int line;
    int column;
    std::string message;
};
```

`RuntimeResult<T>`의 `.error()` 반환 타입 확인 후 LSP 코드에서 맞게 사용.

---

## Step 5: CMakeLists / vcxproj 업데이트

`zephyr_cli.vcxproj`를 읽어 `cli/main.cpp`가 포함된 ItemGroup에 `cli/lsp_server.cpp` 추가.
(별도 파일로 분리한 경우)

또는 cli/main.cpp 내에 직접 구현해도 무방 (파일이 커지면 분리).

---

## Step 6: 빌드 + 수동 테스트

### 빌드
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

### 수동 smoke test (PowerShell)
```powershell
# initialize 요청 전송
$body = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
$msg = "Content-Length: $($body.Length)`r`n`r`n$body"
$msg | & x64\Release\zephyr_cli.exe lsp
```

정상이면 `Content-Length: ...` 응답 출력.

### 기존 테스트 회귀
```powershell
x64\Release\zephyr_tests.exe
```
5/5 PASS 유지.

---

## 주의사항
- stdout은 LSP 전용 → 모든 디버그 출력은 `std::cerr`로
- `\n`과 `\r\n` 혼용 주의 (헤더는 `\r\n`, JSON 본문은 `\n` 가능)
- Windows stdin binary mode 설정 필요:
  ```cpp
  #ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  #endif
  ```
- `api.hpp`의 `parse_source`가 비공개면 `check_file()` 함수 내부 로직을 참고해 오류 추출
