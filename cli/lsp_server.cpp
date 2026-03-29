#include "zephyr/api.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// JSON-RPC transport
// ─────────────────────────────────────────────────────────────────────────────

std::string lsp_read_message() {
    std::string line;
    int content_length = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        if (line.rfind("Content-Length:", 0) == 0) {
            content_length = std::stoi(line.substr(15));
        }
    }
    if (content_length == 0) return "";
    std::string body(content_length, '\0');
    std::cin.read(body.data(), content_length);
    return body;
}

void lsp_send_message(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal JSON helpers (string-search based, no external library)
// ─────────────────────────────────────────────────────────────────────────────

std::string lsp_extract_string(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    // Handle escaped characters simply by scanning until unescaped '"'
    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == '"') { result += '"'; pos += 2; continue; }
            if (next == '\\') { result += '\\'; pos += 2; continue; }
            if (next == 'n') { result += '\n'; pos += 2; continue; }
            if (next == 'r') { result += '\r'; pos += 2; continue; }
            if (next == 't') { result += '\t'; pos += 2; continue; }
        }
        if (c == '"') break;
        result += c;
        ++pos;
    }
    return result;
}

int lsp_extract_int(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return -1;
    pos += search.size();
    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return -1;
    try { return std::stoi(json.substr(pos)); } catch (...) { return -1; }
}

// Escape a string for embedding in JSON
std::string lsp_escape_string(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == '"')       result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else                result += c;
    }
    return result;
}

// Extract the text content of a textDocument object: params.textDocument.text
std::string lsp_extract_text_document_text(const std::string& json) {
    // Find "textDocument" object then extract "text" inside it
    const std::string td_key = "\"textDocument\":{";
    auto td_pos = json.find(td_key);
    if (td_pos == std::string::npos) {
        // Also try without space
        td_pos = json.find("\"textDocument\": {");
    }
    if (td_pos != std::string::npos) {
        // Search "text": after this position
        const std::string sub = json.substr(td_pos);
        return lsp_extract_string(sub, "text");
    }
    return lsp_extract_string(json, "text");
}

// Extract contentChanges[0].text from a didChange notification
std::string lsp_extract_change_text(const std::string& json) {
    const std::string cc_key = "\"contentChanges\":[{";
    auto pos = json.find(cc_key);
    if (pos == std::string::npos) return "";
    return lsp_extract_string(json.substr(pos), "text");
}

// Extract word at (line, character) position from source
std::string extract_word_at(const std::string& source, int line, int character) {
    std::istringstream ss(source);
    std::string ln;
    int cur = 0;
    while (std::getline(ss, ln)) {
        if (cur == line) {
            if (character < 0 || character >= static_cast<int>(ln.size())) return "";
            // Find word boundaries
            int start = character;
            while (start > 0 && (std::isalnum(ln[start - 1]) || ln[start - 1] == '_')) --start;
            int end = character;
            while (end < static_cast<int>(ln.size()) && (std::isalnum(ln[end]) || ln[end] == '_')) ++end;
            return ln.substr(start, end - start);
        }
        ++cur;
    }
    return "";
}

// Parse "uri:line:col: message" from ZephyrRuntimeError::what()
struct ParsedError {
    int line = 0;      // 1-based from runtime, will convert to 0-based for LSP
    int column = 0;    // 1-based from runtime
    std::string message;
};

ParsedError parse_runtime_error(const std::string& uri, const std::string& error_what) {
    ParsedError result;
    result.message = error_what;

    // The format is: "uri:line:col: message" or "module_name:line:col: message"
    // uri may be a file:// URL or just a path – try both.
    // We need to find ":LINE:COL: " after the URI/module name.
    std::string search_prefix = uri + ":";
    auto pos = error_what.find(search_prefix);
    if (pos == std::string::npos) {
        // Fallback: look for pattern ":digit:" near start
        pos = 0;
    } else {
        pos += search_prefix.size();
    }

    // Try to parse "LINE:COL: " from pos
    try {
        std::size_t consumed = 0;
        int ln = std::stoi(error_what.substr(pos), &consumed);
        pos += consumed;
        if (pos < error_what.size() && error_what[pos] == ':') {
            ++pos;
            std::size_t consumed2 = 0;
            int col = std::stoi(error_what.substr(pos), &consumed2);
            pos += consumed2;
            result.line = ln;
            result.column = col;
            // Skip ": "
            if (pos < error_what.size() && error_what[pos] == ':') ++pos;
            if (pos < error_what.size() && error_what[pos] == ' ') ++pos;
            result.message = error_what.substr(pos);
        }
    } catch (...) {
        // Couldn't parse, keep defaults
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Server state
// ─────────────────────────────────────────────────────────────────────────────

struct LspServer {
    zephyr::ZephyrVM vm;
    std::unordered_map<std::string, std::string> open_documents;  // uri → source
};

// ─────────────────────────────────────────────────────────────────────────────
// LSP feature helpers
// ─────────────────────────────────────────────────────────────────────────────

void lsp_publish_diagnostics(LspServer& server, const std::string& uri, const std::string& source) {
    std::ostringstream diags;
    diags << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << lsp_escape_string(uri) << "\",\"diagnostics\":[";

    try {
        server.vm.check_string(source, uri, std::filesystem::current_path());
        // No errors — emit empty diagnostics list
    } catch (const std::exception& ex) {
        const auto err = parse_runtime_error(uri, ex.what());
        // Convert 1-based line/col to 0-based LSP positions
        int lsp_line = (err.line > 0) ? (err.line - 1) : 0;
        int lsp_col  = (err.column > 0) ? (err.column - 1) : 0;
        diags << "{\"range\":{\"start\":{\"line\":" << lsp_line
              << ",\"character\":" << lsp_col
              << "},\"end\":{\"line\":" << lsp_line
              << ",\"character\":" << (lsp_col + 1)
              << "}},\"severity\":1,\"message\":\""
              << lsp_escape_string(err.message) << "\"}";
    }

    diags << "]}}";
    lsp_send_message(diags.str());
}

std::string lsp_hover_content(const std::string& source, int line, int character) {
    const std::string word = extract_word_at(source, line, character);
    if (word.empty()) return "{}";

    static const std::unordered_map<std::string, std::string> builtin_docs = {
        {"print",  "print(value: Any) -> Nil\nConsole output"},
        {"len",    "len(s: String) -> Int\nString length"},
        {"fn",     "fn name(params) -> ReturnType { ... }\nFunction declaration"},
        {"let",    "let name = value\nVariable declaration"},
        {"yield",  "yield value\nYield from coroutine"},
        {"if",     "if condition { ... } else { ... }\nConditional expression"},
        {"else",   "else { ... }\nAlternative branch"},
        {"while",  "while condition { ... }\nLoop while condition is true"},
        {"for",    "for item in collection { ... }\nIterate over a collection"},
        {"return", "return value\nReturn a value from a function"},
        {"true",   "true\nBoolean literal"},
        {"false",  "false\nBoolean literal"},
        {"nil",    "nil\nNull/absent value"},
        {"import", "import module\nImport a module"},
        {"export", "export name\nExport a name from the current module"},
        {"trait",  "trait Name { ... }\nDefine a trait (interface)"},
        {"impl",   "impl TraitName for TypeName { ... }\nImplement a trait for a type"},
        {"match",  "match value { pattern => expr, ... }\nPattern matching expression"},
        {"struct", "struct Name { field: Type, ... }\nDefine a struct type"},
        {"class",  "class Name { ... }\nDefine a class"},
        {"int",    "int\nInteger type (64-bit signed)"},
        {"float",  "float\nFloating-point type (64-bit)"},
        {"string", "string\nString type (UTF-8)"},
        {"bool",   "bool\nBoolean type"},
    };

    auto it = builtin_docs.find(word);
    if (it != builtin_docs.end()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```zephyr\\n"
               + lsp_escape_string(it->second) + "\\n```\"}}";
    }
    return "{}";
}

std::string lsp_completion_items() {
    static const std::vector<std::string> keywords = {
        "fn", "let", "if", "else", "while", "for", "return", "yield",
        "true", "false", "nil", "import", "export", "trait", "impl",
        "match", "struct", "class", "int", "float", "string", "bool"
    };

    std::ostringstream items;
    items << "[";
    for (std::size_t i = 0; i < keywords.size(); ++i) {
        if (i > 0) items << ",";
        items << "{\"label\":\"" << keywords[i] << "\",\"kind\":14}";
    }
    items << "]";
    return items.str();
}

std::string lsp_find_definition(const std::string& source, const std::string& uri, const std::string& word) {
    if (word.empty()) return "null";
    std::istringstream ss(source);
    std::string line;
    int line_num = 0;
    while (std::getline(ss, line)) {
        const std::string pattern = "fn " + word + "(";
        const auto col = line.find(pattern);
        if (col != std::string::npos) {
            return "{\"uri\":\"" + lsp_escape_string(uri)
                 + "\",\"range\":{\"start\":{\"line\":" + std::to_string(line_num)
                 + ",\"character\":" + std::to_string(col)
                 + "},\"end\":{\"line\":" + std::to_string(line_num)
                 + ",\"character\":" + std::to_string(col + word.size()) + "}}}";
        }
        ++line_num;
    }
    return "null";
}

std::string lsp_document_symbols(const std::string& source, const std::string& uri) {
    // Scan for top-level "fn name(" patterns
    std::istringstream ss(source);
    std::string line;
    int line_num = 0;
    bool first = true;
    std::ostringstream out;
    out << "[";
    while (std::getline(ss, line)) {
        const std::string prefix = "fn ";
        auto fn_pos = line.find(prefix);
        if (fn_pos != std::string::npos) {
            std::size_t name_start = fn_pos + prefix.size();
            // skip whitespace
            while (name_start < line.size() && line[name_start] == ' ') ++name_start;
            std::size_t name_end = name_start;
            while (name_end < line.size() && (std::isalnum(line[name_end]) || line[name_end] == '_')) ++name_end;
            if (name_end > name_start) {
                const std::string fn_name = line.substr(name_start, name_end - name_start);
                if (!first) out << ",";
                first = false;
                // kind 12 = Function
                out << "{\"name\":\"" << lsp_escape_string(fn_name)
                    << "\",\"kind\":12"
                    << ",\"location\":{\"uri\":\"" << lsp_escape_string(uri)
                    << "\",\"range\":{\"start\":{\"line\":" << line_num
                    << ",\"character\":" << fn_pos
                    << "},\"end\":{\"line\":" << line_num
                    << ",\"character\":" << name_end << "}}}}";
            }
        }
        ++line_num;
    }
    out << "]";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Request dispatcher
// ─────────────────────────────────────────────────────────────────────────────

bool lsp_dispatch(LspServer& server, const std::string& msg) {
    const std::string method = lsp_extract_string(msg, "method");
    const int id_int = lsp_extract_int(msg, "id");
    const std::string id_str = (id_int >= 0) ? std::to_string(id_int) : "null";

    std::cerr << "[lsp] method=" << method << " id=" << id_str << "\n";

    if (method == "initialize") {
        const std::string response =
            "{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ","
            "\"result\":{"
                "\"capabilities\":{"
                    "\"textDocumentSync\":1,"
                    "\"hoverProvider\":true,"
                    "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                    "\"definitionProvider\":true,"
                    "\"documentSymbolProvider\":true"
                "},"
                "\"serverInfo\":{\"name\":\"zephyr-lsp\",\"version\":\"0.1.0\"}"
            "}}";
        lsp_send_message(response);
        return true;
    }

    if (method == "initialized") {
        // No response needed
        return true;
    }

    if (method == "textDocument/didOpen") {
        const std::string uri    = lsp_extract_string(msg, "uri");
        const std::string source = lsp_extract_text_document_text(msg);
        server.open_documents[uri] = source;
        lsp_publish_diagnostics(server, uri, source);
        return true;
    }

    if (method == "textDocument/didChange") {
        const std::string uri    = lsp_extract_string(msg, "uri");
        const std::string source = lsp_extract_change_text(msg);
        if (!source.empty()) {
            server.open_documents[uri] = source;
            lsp_publish_diagnostics(server, uri, source);
        }
        return true;
    }

    if (method == "textDocument/didSave") {
        const std::string uri = lsp_extract_string(msg, "uri");
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            lsp_publish_diagnostics(server, uri, it->second);
        }
        return true;
    }

    if (method == "textDocument/didClose") {
        const std::string uri = lsp_extract_string(msg, "uri");
        server.open_documents.erase(uri);
        // Clear diagnostics
        const std::string clear_diags =
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
            "\"params\":{\"uri\":\"" + lsp_escape_string(uri) + "\",\"diagnostics\":[]}}";
        lsp_send_message(clear_diags);
        return true;
    }

    if (method == "textDocument/hover") {
        const std::string uri = lsp_extract_string(msg, "uri");
        // Extract position from params.position
        const int line = lsp_extract_int(msg, "line");
        const int character = lsp_extract_int(msg, "character");
        std::string contents = "{}";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            contents = lsp_hover_content(it->second, line, character);
        }
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + contents + "}");
        return true;
    }

    if (method == "textDocument/completion") {
        const std::string items = lsp_completion_items();
        lsp_send_message(
            "{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ","
            "\"result\":{\"isIncomplete\":false,\"items\":" + items + "}}");
        return true;
    }

    if (method == "textDocument/definition") {
        const std::string uri = lsp_extract_string(msg, "uri");
        const int line = lsp_extract_int(msg, "line");
        const int character = lsp_extract_int(msg, "character");
        std::string location = "null";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            const std::string word = extract_word_at(it->second, line, character);
            location = lsp_find_definition(it->second, uri, word);
        }
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + location + "}");
        return true;
    }

    if (method == "textDocument/documentSymbol") {
        const std::string uri = lsp_extract_string(msg, "uri");
        std::string symbols = "[]";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            symbols = lsp_document_symbols(it->second, uri);
        }
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + symbols + "}");
        return true;
    }

    if (method == "shutdown") {
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":null}");
        return true;
    }

    if (method == "exit") {
        return false;  // Signal to break the main loop
    }

    // For unknown requests with an id, send a "method not found" error
    if (id_int >= 0) {
        lsp_send_message(
            "{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ","
            "\"error\":{\"code\":-32601,\"message\":\"Method not found: " + lsp_escape_string(method) + "\"}}");
    }
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Entry point (declared in main.cpp)
// ─────────────────────────────────────────────────────────────────────────────

int run_lsp_server(const char* exe_path) {
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    LspServer server;
    if (exe_path && exe_path[0] != '\0') {
        const auto exe_dir = std::filesystem::weakly_canonical(
            std::filesystem::path(exe_path)).parent_path();
        server.vm.add_module_search_path(exe_dir.string());
    }
    server.vm.add_module_search_path(std::filesystem::current_path().string());

    std::cerr << "[lsp] zephyr-lsp server started\n";

    while (true) {
        const std::string msg = lsp_read_message();
        if (msg.empty()) break;
        if (!lsp_dispatch(server, msg)) break;
    }

    std::cerr << "[lsp] server exiting\n";
    return 0;
}
