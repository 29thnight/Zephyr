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
// Source symbol extraction (regex-free, line-scan based)
// ─────────────────────────────────────────────────────────────────────────────

struct SourceSymbol {
    std::string name;
    int line = 0;   // 0-based
    int col  = 0;   // 0-based
    std::string kind;       // "fn", "struct", "trait", "let"
    std::string signature;  // display text for hover/completion
};

static std::size_t scan_identifier(const std::string& line, std::size_t pos) {
    while (pos < line.size() && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) ++pos;
    return pos;
}

static bool word_boundary_before(const std::string& line, std::size_t pos) {
    return pos == 0 || !std::isalnum(static_cast<unsigned char>(line[pos - 1]));
}

std::vector<SourceSymbol> extract_source_symbols(const std::string& source) {
    std::vector<SourceSymbol> symbols;
    std::istringstream ss(source);
    std::string ln;
    int lnum = 0;

    auto try_extract = [&](const std::string& keyword, const std::string& kind) {
        std::size_t p = 0;
        while ((p = ln.find(keyword, p)) != std::string::npos) {
            if (!word_boundary_before(ln, p)) { ++p; continue; }
            std::size_t after = p + keyword.size();
            if (after >= ln.size() || std::isalnum(static_cast<unsigned char>(ln[after])) || ln[after] == '_') { ++p; continue; }
            while (after < ln.size() && ln[after] == ' ') ++after;
            std::size_t ne = scan_identifier(ln, after);
            if (ne > after) {
                SourceSymbol sym;
                sym.name = ln.substr(after, ne - after);
                sym.line = lnum;
                sym.col  = static_cast<int>(p);
                sym.kind = kind;
                if (kind == "fn") {
                    std::size_t brace = ln.find('{');
                    std::string sig = ln.substr(p, brace != std::string::npos ? brace - p : std::string::npos);
                    while (!sig.empty() && std::isspace(static_cast<unsigned char>(sig.back()))) sig.pop_back();
                    sym.signature = sig;
                } else if (kind == "let") {
                    std::string sig = ln.substr(p);
                    // trim at semicolon
                    auto sc = sig.find(';');
                    if (sc != std::string::npos) sig = sig.substr(0, sc);
                    while (!sig.empty() && std::isspace(static_cast<unsigned char>(sig.back()))) sig.pop_back();
                    sym.signature = sig;
                } else {
                    sym.signature = kind + " " + sym.name;
                }
                symbols.push_back(std::move(sym));
            }
            ++p;
        }
    };

    while (std::getline(ss, ln)) {
        try_extract("fn ",     "fn");
        try_extract("struct ", "struct");
        try_extract("trait ",  "trait");
        try_extract("let ",    "let");
        ++lnum;
    }
    return symbols;
}

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

// Infer a simple type label from a let-binding RHS (best-effort, no AST)
static std::string infer_let_type(const std::string& rhs) {
    if (rhs.empty()) return "";
    // Strip leading whitespace
    std::size_t p = 0;
    while (p < rhs.size() && std::isspace(static_cast<unsigned char>(rhs[p]))) ++p;
    const std::string v = rhs.substr(p);
    if (v.empty()) return "";
    if (v == "true" || v == "false") return "bool";
    if (v.front() == '"') return "string";
    if (v.front() == '[') return "array";
    // Check for float: contains '.' or 'e'/'E' and starts with digit or '-'
    bool has_dot = v.find('.') != std::string::npos;
    bool has_e   = v.find('e') != std::string::npos || v.find('E') != std::string::npos;
    bool is_num  = std::isdigit(static_cast<unsigned char>(v.front())) || (v.front() == '-' && v.size() > 1 && std::isdigit(static_cast<unsigned char>(v[1])));
    if (is_num && (has_dot || has_e)) return "float";
    if (is_num) return "int";
    // Struct literal: starts with uppercase
    if (std::isupper(static_cast<unsigned char>(v.front()))) {
        std::size_t ne = scan_identifier(v, 0);
        return v.substr(0, ne);
    }
    return "";
}

// Build hover markdown for a user-defined symbol
static std::string hover_for_symbol(const SourceSymbol& sym) {
    std::string text;
    if (sym.kind == "let") {
        // Try to extract RHS for type inference
        const auto eq = sym.signature.find('=');
        std::string type_label;
        if (eq != std::string::npos) {
            type_label = infer_let_type(sym.signature.substr(eq + 1));
        }
        if (!type_label.empty()) {
            text = "**let** `" + sym.name + "`: `" + type_label + "`";
        } else {
            text = "**(" + sym.kind + ")** `" + sym.signature + "`";
        }
    } else {
        text = "**(" + sym.kind + ")** `" + sym.signature + "`";
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + lsp_escape_string(text) + "\"}}";
}

std::string lsp_hover_content(const std::string& source, int line, int character) {
    const std::string word = extract_word_at(source, line, character);
    if (word.empty()) return "{}";

    static const std::unordered_map<std::string, std::string> builtin_docs = {
        {"print",   "print(value: any) -> void\nConsole output"},
        {"println", "println(value: any) -> void\nConsole output with newline"},
        {"len",     "len(s: string) -> int\nString or array length"},
        {"fn",      "fn name(params) -> ReturnType { ... }\nFunction declaration"},
        {"let",     "let name = value\nVariable declaration"},
        {"yield",   "yield value\nYield from coroutine"},
        {"if",      "if condition { ... } else { ... }\nConditional expression"},
        {"else",    "else { ... }\nAlternative branch"},
        {"while",   "while condition { ... }\nLoop while condition is true"},
        {"for",     "for item in collection { ... }\nIterate over a collection"},
        {"return",  "return value\nReturn a value from a function"},
        {"true",    "true\nBoolean literal"},
        {"false",   "false\nBoolean literal"},
        {"nil",     "nil\nNull/absent value"},
        {"import",  "import \"module\"\nImport a module"},
        {"export",  "export name\nExport a name from the current module"},
        {"trait",   "trait Name { fn method(self) -> Type }\nDefine a trait (interface)"},
        {"impl",    "impl TraitName for TypeName { ... }\nImplement a trait for a type"},
        {"match",   "match value { pattern => expr, _ => default }\nPattern matching"},
        {"struct",  "struct Name { field: type, ... }\nDefine a struct type"},
        {"int",     "int\nInteger type (64-bit signed)"},
        {"float",   "float\nFloating-point type (64-bit)"},
        {"string",  "string\nString type (UTF-8)"},
        {"bool",    "bool\nBoolean type (true/false)"},
        {"void",    "void\nNo-value return type"},
        {"any",     "any\nDynamic type"},
        {"Ok",      "Ok(value)\nResult success variant"},
        {"Err",     "Err(error)\nResult error variant"},
        {"where",   "where T: Trait\nTrait constraint in generic functions"},
    };

    auto it = builtin_docs.find(word);
    if (it != builtin_docs.end()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```zephyr\\n"
               + lsp_escape_string(it->second) + "\\n```\"}}";
    }

    // Search source symbols for a matching definition
    const auto symbols = extract_source_symbols(source);
    for (const auto& sym : symbols) {
        if (sym.name == word && !sym.signature.empty()) {
            return hover_for_symbol(sym);
        }
    }

    return "{}";
}

// ─────────────────────────────────────────────────────────────────────────────
// signatureHelp: parse active function call at cursor
// ─────────────────────────────────────────────────────────────────────────────

std::string lsp_signature_help(const std::string& source, int line, int character) {
    // Walk the current line backwards from cursor to find "funcName("
    std::istringstream ss(source);
    std::string ln;
    int cur = 0;
    while (std::getline(ss, ln)) {
        if (cur == line) break;
        ++cur;
    }
    if (cur != line || ln.empty()) return "null";

    int end = std::min(character, static_cast<int>(ln.size()));
    // Find the innermost open paren
    int paren_depth = 0;
    int active_param = 0;
    int fn_end = -1;
    for (int i = end - 1; i >= 0; --i) {
        char c = ln[i];
        if (c == ')') { ++paren_depth; continue; }
        if (c == '(') {
            if (paren_depth > 0) { --paren_depth; continue; }
            fn_end = i;
            break;
        }
        if (c == ',' && paren_depth == 0) ++active_param;
    }
    if (fn_end < 0) return "null";

    // Extract function name before '('
    int ne = fn_end - 1;
    while (ne >= 0 && std::isspace(static_cast<unsigned char>(ln[ne]))) --ne;
    int ns = ne;
    while (ns > 0 && (std::isalnum(static_cast<unsigned char>(ln[ns - 1])) || ln[ns - 1] == '_')) --ns;
    if (ns > ne) return "null";
    const std::string fn_name = ln.substr(ns, ne - ns + 1);
    if (fn_name.empty()) return "null";

    // Find matching fn symbol
    const auto symbols = extract_source_symbols(source);
    for (const auto& sym : symbols) {
        if (sym.kind != "fn" || sym.name != fn_name) continue;
        // Extract parameter list from signature
        const auto lp = sym.signature.find('(');
        const auto rp = sym.signature.rfind(')');
        std::string params_str;
        if (lp != std::string::npos && rp != std::string::npos && rp > lp)
            params_str = sym.signature.substr(lp + 1, rp - lp - 1);

        // Split params by comma
        std::vector<std::string> params;
        std::string cur_param;
        int depth2 = 0;
        for (char c : params_str) {
            if (c == '<' || c == '(') ++depth2;
            else if (c == '>' || c == ')') --depth2;
            else if (c == ',' && depth2 == 0) {
                // trim
                while (!cur_param.empty() && std::isspace(static_cast<unsigned char>(cur_param.front()))) cur_param.erase(cur_param.begin());
                while (!cur_param.empty() && std::isspace(static_cast<unsigned char>(cur_param.back()))) cur_param.pop_back();
                params.push_back(cur_param);
                cur_param.clear();
                continue;
            }
            cur_param += c;
        }
        while (!cur_param.empty() && std::isspace(static_cast<unsigned char>(cur_param.front()))) cur_param.erase(cur_param.begin());
        while (!cur_param.empty() && std::isspace(static_cast<unsigned char>(cur_param.back()))) cur_param.pop_back();
        if (!cur_param.empty()) params.push_back(cur_param);

        // Build parameters JSON array
        std::ostringstream pjson;
        pjson << "[";
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i > 0) pjson << ",";
            pjson << "{\"label\":\"" << lsp_escape_string(params[i]) << "\"}";
        }
        pjson << "]";

        const int clamp_param = std::min(active_param, static_cast<int>(params.size()) - 1);
        return "{\"signatures\":[{\"label\":\"" + lsp_escape_string(sym.signature)
             + "\",\"parameters\":" + pjson.str() + "}]"
             + ",\"activeSignature\":0,\"activeParameter\":" + std::to_string(std::max(0, clamp_param)) + "}";
    }
    return "null";
}

std::string lsp_completion_items(const std::string& source) {
    static const std::vector<std::string> keywords = {
        "fn", "let", "if", "else", "while", "for", "return", "yield",
        "true", "false", "nil", "import", "export", "trait", "impl",
        "match", "struct", "int", "float", "string", "bool", "void",
        "any", "Ok", "Err", "where"
    };

    std::ostringstream items;
    items << "[";
    bool first = true;

    for (const auto& kw : keywords) {
        if (!first) items << ",";
        first = false;
        items << "{\"label\":\"" << kw << "\",\"kind\":14}";
    }

    // Source-defined symbols
    const auto symbols = extract_source_symbols(source);
    for (const auto& sym : symbols) {
        if (!first) items << ",";
        first = false;
        // LSP completion kinds: 3=Function, 22=Struct, 8=Interface(trait), 6=Variable
        int kind = 6;
        if      (sym.kind == "fn")     kind = 3;
        else if (sym.kind == "struct") kind = 22;
        else if (sym.kind == "trait")  kind = 8;

        const std::string detail = sym.signature.empty() ? sym.name : sym.signature;
        items << "{\"label\":\"" << lsp_escape_string(sym.name)
              << "\",\"kind\":" << kind
              << ",\"detail\":\"" << lsp_escape_string(detail) << "\"}";
    }

    items << "]";
    return items.str();
}

std::string lsp_find_definition(const std::string& source, const std::string& uri, const std::string& word) {
    if (word.empty()) return "null";
    const auto symbols = extract_source_symbols(source);
    for (const auto& sym : symbols) {
        if (sym.name == word) {
            return "{\"uri\":\"" + lsp_escape_string(uri)
                 + "\",\"range\":{\"start\":{\"line\":" + std::to_string(sym.line)
                 + ",\"character\":" + std::to_string(sym.col)
                 + "},\"end\":{\"line\":" + std::to_string(sym.line)
                 + ",\"character\":" + std::to_string(sym.col + static_cast<int>(word.size())) + "}}}";
        }
    }
    return "null";
}

std::string lsp_document_symbols(const std::string& source, const std::string& uri) {
    const auto symbols = extract_source_symbols(source);
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& sym : symbols) {
        // LSP symbol kinds: 12=Function, 23=Struct, 11=Interface(trait), 13=Variable
        int kind = 13;
        if      (sym.kind == "fn")     kind = 12;
        else if (sym.kind == "struct") kind = 23;
        else if (sym.kind == "trait")  kind = 11;

        if (!first) out << ",";
        first = false;
        out << "{\"name\":\"" << lsp_escape_string(sym.name)
            << "\",\"kind\":" << kind
            << ",\"location\":{\"uri\":\"" << lsp_escape_string(uri)
            << "\",\"range\":{\"start\":{\"line\":" << sym.line
            << ",\"character\":" << sym.col
            << "},\"end\":{\"line\":" << sym.line
            << ",\"character\":" << (sym.col + static_cast<int>(sym.name.size())) << "}}}}";
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
                    "\"referencesProvider\":true,"
                    "\"documentSymbolProvider\":true,"
                    "\"workspaceSymbolProvider\":true,"
                    "\"renameProvider\":true,"
                    "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}"
                "},"
                "\"serverInfo\":{\"name\":\"zephyr-lsp\",\"version\":\"0.2.0\"}"
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
        std::string items = "[]";
        {
            const std::string uri = lsp_extract_string(msg, "uri");
            auto it = server.open_documents.find(uri);
            items = lsp_completion_items(it != server.open_documents.end() ? it->second : "");
        }
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

    if (method == "textDocument/signatureHelp") {
        const std::string uri = lsp_extract_string(msg, "uri");
        const int line = lsp_extract_int(msg, "line");
        const int character = lsp_extract_int(msg, "character");
        std::string result = "null";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            result = lsp_signature_help(it->second, line, character);
        }
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + result + "}");
        return true;
    }

    if (method == "textDocument/rename") {
        const std::string uri = lsp_extract_string(msg, "uri");
        const int line = lsp_extract_int(msg, "line");
        const int character = lsp_extract_int(msg, "character");
        const std::string new_name = lsp_extract_string(msg, "newName");
        std::string result = "null";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end() && !new_name.empty()) {
            const std::string word = extract_word_at(it->second, line, character);
            if (!word.empty()) {
                // Collect all reference ranges (same logic as references handler)
                std::ostringstream edits;
                edits << "[";
                std::istringstream sstream(it->second);
                std::string ln2;
                int lnum = 0;
                bool first_edit = true;
                while (std::getline(sstream, ln2)) {
                    std::size_t pos = 0;
                    while ((pos = ln2.find(word, pos)) != std::string::npos) {
                        bool before_ok = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(ln2[pos - 1])) && ln2[pos - 1] != '_'));
                        std::size_t after_pos = pos + word.size();
                        bool after_ok  = (after_pos >= ln2.size() || (!std::isalnum(static_cast<unsigned char>(ln2[after_pos])) && ln2[after_pos] != '_'));
                        if (before_ok && after_ok) {
                            if (!first_edit) edits << ",";
                            first_edit = false;
                            edits << "{\"range\":{\"start\":{\"line\":" << lnum
                                  << ",\"character\":" << pos
                                  << "},\"end\":{\"line\":" << lnum
                                  << ",\"character\":" << after_pos
                                  << "}},\"newText\":\"" << lsp_escape_string(new_name) << "\"}";
                        }
                        ++pos;
                    }
                    ++lnum;
                }
                edits << "]";
                result = "{\"changes\":{\"" + lsp_escape_string(uri) + "\":" + edits.str() + "}}";
            }
        }
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + result + "}");
        return true;
    }

    if (method == "workspace/symbol") {
        const std::string query = lsp_extract_string(msg, "query");
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const auto& [uri, source] : server.open_documents) {
            const auto symbols = extract_source_symbols(source);
            for (const auto& sym : symbols) {
                // Filter by query (empty query = return all)
                if (!query.empty() && sym.name.find(query) == std::string::npos) continue;
                int kind = 13;  // Variable
                if      (sym.kind == "fn")     kind = 12;
                else if (sym.kind == "struct") kind = 23;
                else if (sym.kind == "trait")  kind = 11;
                if (!first) out << ",";
                first = false;
                out << "{\"name\":\"" << lsp_escape_string(sym.name)
                    << "\",\"kind\":" << kind
                    << ",\"location\":{\"uri\":\"" << lsp_escape_string(uri)
                    << "\",\"range\":{\"start\":{\"line\":" << sym.line
                    << ",\"character\":" << sym.col
                    << "},\"end\":{\"line\":" << sym.line
                    << ",\"character\":" << (sym.col + static_cast<int>(sym.name.size()))
                    << "}}}}";
            }
        }
        out << "]";
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + out.str() + "}");
        return true;
    }

    if (method == "textDocument/references") {
        const std::string uri = lsp_extract_string(msg, "uri");
        const int line = lsp_extract_int(msg, "line");
        const int character = lsp_extract_int(msg, "character");
        std::ostringstream refs;
        refs << "[";
        auto it = server.open_documents.find(uri);
        if (it != server.open_documents.end()) {
            const std::string word = extract_word_at(it->second, line, character);
            if (!word.empty()) {
                std::istringstream ss(it->second);
                std::string ln;
                int lnum = 0;
                bool first_ref = true;
                while (std::getline(ss, ln)) {
                    std::size_t pos = 0;
                    while ((pos = ln.find(word, pos)) != std::string::npos) {
                        // Verify word boundaries
                        bool before_ok = (pos == 0 || !std::isalnum(static_cast<unsigned char>(ln[pos - 1])) && ln[pos - 1] != '_');
                        std::size_t after_pos = pos + word.size();
                        bool after_ok  = (after_pos >= ln.size() || (!std::isalnum(static_cast<unsigned char>(ln[after_pos])) && ln[after_pos] != '_'));
                        if (before_ok && after_ok) {
                            if (!first_ref) refs << ",";
                            first_ref = false;
                            refs << "{\"uri\":\"" << lsp_escape_string(uri)
                                 << "\",\"range\":{\"start\":{\"line\":" << lnum
                                 << ",\"character\":" << pos
                                 << "},\"end\":{\"line\":" << lnum
                                 << ",\"character\":" << after_pos << "}}}";
                        }
                        ++pos;
                    }
                    ++lnum;
                }
            }
        }
        refs << "]";
        lsp_send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + refs.str() + "}");
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
