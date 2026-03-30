#include "zephyr/api.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// ─── Sequence counter ────────────────────────────────────────────────────────

static std::atomic<int> g_seq{1};
static int next_seq() { return g_seq.fetch_add(1, std::memory_order_relaxed); }

// ─── Transport (Content-Length framing) ──────────────────────────────────────

std::string dap_read_message() {
    std::string line;
    int content_length = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        if (line.rfind("Content-Length:", 0) == 0)
            content_length = std::stoi(line.substr(15));
    }
    if (content_length <= 0) return "";
    std::string body(static_cast<std::size_t>(content_length), '\0');
    std::cin.read(body.data(), content_length);
    return body;
}

static std::mutex g_out_mutex;
void dap_send(const std::string& body) {
    std::lock_guard<std::mutex> lk(g_out_mutex);
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

// ─── Minimal JSON helpers ─────────────────────────────────────────────────────

std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == '\t') r += "\\t";
        else r += c;
    }
    return r;
}

std::string dap_extract_string(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char n = json[pos + 1];
            if (n == '"') { result += '"'; pos += 2; continue; }
            if (n == '\\') { result += '\\'; pos += 2; continue; }
            if (n == 'n') { result += '\n'; pos += 2; continue; }
        }
        if (c == '"') break;
        result += c;
        ++pos;
    }
    return result;
}

int dap_extract_int(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return -1;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return -1;
    try { return std::stoi(json.substr(pos)); } catch (...) { return -1; }
}

// Extract the "command" field
std::string dap_command(const std::string& json) { return dap_extract_string(json, "command"); }
int         dap_req_seq(const std::string& json)  { return dap_extract_int(json, "seq"); }

// ─── DAP message builders ────────────────────────────────────────────────────

void send_response(int req_seq, const std::string& command, bool success,
                   const std::string& body = "") {
    std::ostringstream out;
    out << "{\"seq\":" << next_seq()
        << ",\"type\":\"response\""
        << ",\"request_seq\":" << req_seq
        << ",\"success\":" << (success ? "true" : "false")
        << ",\"command\":\"" << json_escape(command) << "\"";
    if (!body.empty())
        out << ",\"body\":" << body;
    out << "}";
    dap_send(out.str());
}

void send_error_response(int req_seq, const std::string& command, const std::string& message) {
    std::ostringstream out;
    out << "{\"seq\":" << next_seq()
        << ",\"type\":\"response\""
        << ",\"request_seq\":" << req_seq
        << ",\"success\":false"
        << ",\"command\":\"" << json_escape(command) << "\""
        << ",\"body\":{\"error\":{\"id\":1,\"format\":\""
        << json_escape(message) << "\"}}}";
    dap_send(out.str());
}

void send_event(const std::string& event, const std::string& body = "{}") {
    std::ostringstream out;
    out << "{\"seq\":" << next_seq()
        << ",\"type\":\"event\""
        << ",\"event\":\"" << json_escape(event) << "\""
        << ",\"body\":" << body << "}";
    dap_send(out.str());
}

// ─── Server state ─────────────────────────────────────────────────────────────

struct DapBreakpoint {
    std::string source;
    int line = 0;
};

struct DapServer {
    zephyr::ZephyrVM vm;
    std::filesystem::path exe_path;

    std::string launch_program;           // path to .zph
    bool initialized = false;
    bool terminated  = false;

    std::vector<DapBreakpoint> breakpoints;
    std::string stopped_reason;           // "breakpoint" | "exception" | "entry"

    // Thread executing the script
    std::thread exec_thread;
    std::mutex  state_mutex;

    // Output lines captured from VM execution
    std::vector<std::string> output_lines;
};

// ─── Request handlers ─────────────────────────────────────────────────────────

void handle_initialize(DapServer& s, int req_seq, const std::string& /*msg*/) {
    s.initialized = true;
    const std::string caps =
        "{"
        "\"supportsConfigurationDoneRequest\":true,"
        "\"supportsFunctionBreakpoints\":false,"
        "\"supportsConditionalBreakpoints\":false,"
        "\"supportsEvaluateForHovers\":false,"
        "\"supportsTerminateRequest\":true,"
        "\"supportsBreakpointLocationsRequest\":true"
        "}";
    send_response(req_seq, "initialize", true, caps);
    send_event("initialized");
}

void handle_launch(DapServer& s, int req_seq, const std::string& msg) {
    // Extract "program" from arguments
    const std::string prog = dap_extract_string(msg, "program");
    if (prog.empty()) {
        send_error_response(req_seq, "launch", "Missing 'program' in launch arguments.");
        return;
    }
    s.launch_program = prog;
    send_response(req_seq, "launch", true);
}

void handle_configuration_done(DapServer& s, int req_seq, const std::string& /*msg*/) {
    send_response(req_seq, "configurationDone", true);

    // Register breakpoints with VM
    s.vm.clear_breakpoints();
    for (const auto& bp : s.breakpoints) {
        zephyr::ZephyrBreakpoint zbp;
        zbp.source_file = bp.source;
        zbp.line        = static_cast<std::uint32_t>(bp.line);
        s.vm.set_breakpoint(zbp);
    }

    // Configure VM paths
    s.vm.add_module_search_path(std::filesystem::current_path().string());
    if (!s.exe_path.empty()) {
        s.vm.add_module_search_path(
            std::filesystem::weakly_canonical(s.exe_path).parent_path().string());
    }
    if (!s.launch_program.empty()) {
        const auto prog_dir = std::filesystem::path(s.launch_program).parent_path();
        if (!prog_dir.empty())
            s.vm.add_module_search_path(prog_dir.string());
    }

    // Run program in background thread, stream output events
    s.exec_thread = std::thread([&s]() {
        send_event("process",
            "{\"name\":\"" + json_escape(s.launch_program) + "\""
            ",\"startMethod\":\"launch\"}");

        // Redirect VM output: intercept print() to emit output events
        // (VM already prints to stdout; we intercept via try/catch for errors)
        std::string error_msg;
        try {
            s.vm.execute_file(std::filesystem::path(s.launch_program));
        } catch (const zephyr::ZephyrRuntimeError& e) {
            error_msg = e.message();
            // Emit a stopped event for the error location
            std::ostringstream stopped;
            stopped << "{\"reason\":\"exception\""
                    << ",\"description\":\"" << json_escape(error_msg) << "\""
                    << ",\"threadId\":1,\"allThreadsStopped\":true}";
            send_event("stopped", stopped.str());
        } catch (const std::exception& e) {
            error_msg = e.what();
        }

        if (!error_msg.empty()) {
            send_event("output",
                "{\"category\":\"stderr\",\"output\":\""
                + json_escape(error_msg) + "\\n\"}");
        }

        // Program finished
        send_event("terminated", "{}");
        send_event("exited", "{\"exitCode\":0}");
        s.terminated = true;
    });
}

void handle_set_breakpoints(DapServer& s, int req_seq, const std::string& msg) {
    s.breakpoints.clear();

    // Extract source.path
    const std::string src = dap_extract_string(msg, "path");

    // Parse breakpoints array — extract line numbers simply
    std::vector<int> lines;
    const std::string bp_key = "\"breakpoints\":[";
    auto arr_pos = msg.find(bp_key);
    if (arr_pos != std::string::npos) {
        std::size_t p = arr_pos + bp_key.size();
        while (p < msg.size() && msg[p] != ']') {
            if (msg[p] == '{') {
                // Find "line": N inside this object
                std::size_t obj_end = msg.find('}', p);
                if (obj_end == std::string::npos) break;
                const std::string obj = msg.substr(p, obj_end - p + 1);
                int ln = dap_extract_int(obj, "line");
                if (ln > 0) {
                    s.breakpoints.push_back({src, ln});
                    lines.push_back(ln);
                }
                p = obj_end + 1;
            } else {
                ++p;
            }
        }
    }

    // Build response breakpoints array
    std::ostringstream bps;
    bps << "[";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) bps << ",";
        bps << "{\"verified\":true,\"line\":" << lines[i] << "}";
    }
    bps << "]";
    send_response(req_seq, "setBreakpoints", true, "{\"breakpoints\":" + bps.str() + "}");
}

void handle_threads(int req_seq) {
    send_response(req_seq, "threads", true,
        "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}");
}

void handle_stack_trace(DapServer& s, int req_seq, const std::string& /*msg*/) {
    // Provide a minimal stack frame pointing at the launch file
    const std::string name = s.launch_program.empty() ? "<unknown>"
        : std::filesystem::path(s.launch_program).filename().string();
    std::ostringstream frames;
    frames << "[{\"id\":1,\"name\":\"<main>\""
           << ",\"source\":{\"path\":\"" << json_escape(s.launch_program) << "\"}"
           << ",\"line\":1,\"column\":0}]";
    send_response(req_seq, "stackTrace", true,
        "{\"stackFrames\":" + frames.str() + ",\"totalFrames\":1}");
}

void handle_scopes(int req_seq) {
    send_response(req_seq, "scopes", true,
        "{\"scopes\":["
        "{\"name\":\"Locals\",\"variablesReference\":1,\"expensive\":false}"
        "]}");
}

void handle_variables(DapServer& s, int req_seq, const std::string& /*msg*/) {
    // Enumerate globals exposed by the VM
    std::ostringstream vars;
    vars << "[";
    bool first = true;

    const auto stats = s.vm.runtime_stats();
    // Emit a few useful runtime counters as synthetic variables
    auto add_var = [&](const std::string& name, const std::string& value) {
        if (!first) vars << ",";
        first = false;
        vars << "{\"name\":\"" << json_escape(name) << "\""
             << ",\"value\":\"" << json_escape(value) << "\""
             << ",\"variablesReference\":0}";
    };
    add_var("gc.live_objects",   std::to_string(stats.gc.live_objects));
    add_var("gc.live_bytes",     std::to_string(stats.gc.live_bytes));
    add_var("vm.opcode_count",   std::to_string(stats.vm.opcode_count));
    add_var("coroutine.active",  std::to_string(stats.coroutine.active_coroutines));

    vars << "]";
    send_response(req_seq, "variables", true, "{\"variables\":" + vars.str() + "}");
}

void handle_evaluate(int req_seq, const std::string& msg) {
    const std::string expr = dap_extract_string(msg, "expression");
    // For now return the expression as its own result (no eval support yet)
    send_response(req_seq, "evaluate", true,
        "{\"result\":\"" + json_escape(expr) + "\",\"variablesReference\":0}");
}

void handle_continue(int req_seq) {
    send_response(req_seq, "continue", true, "{\"allThreadsContinued\":true}");
}

void handle_terminate(DapServer& s, int req_seq) {
    s.terminated = true;
    send_response(req_seq, "terminate", true);
    send_event("terminated", "{}");
}

void handle_disconnect(DapServer& s, int req_seq) {
    s.terminated = true;
    send_response(req_seq, "disconnect", true);
}

// ─── Dispatcher ───────────────────────────────────────────────────────────────

// Returns false when session should end
bool dap_dispatch(DapServer& s, const std::string& msg) {
    const std::string cmd = dap_command(msg);
    const int req_seq     = dap_req_seq(msg);

    std::cerr << "[dap] command=" << cmd << " seq=" << req_seq << "\n";

    if (cmd == "initialize")        { handle_initialize(s, req_seq, msg); return true; }
    if (cmd == "launch")            { handle_launch(s, req_seq, msg);     return true; }
    if (cmd == "configurationDone") { handle_configuration_done(s, req_seq, msg); return true; }
    if (cmd == "setBreakpoints")    { handle_set_breakpoints(s, req_seq, msg); return true; }
    if (cmd == "setExceptionBreakpoints") {
        send_response(req_seq, "setExceptionBreakpoints", true, "{}");
        return true;
    }
    if (cmd == "threads")    { handle_threads(req_seq);              return true; }
    if (cmd == "stackTrace") { handle_stack_trace(s, req_seq, msg); return true; }
    if (cmd == "scopes")     { handle_scopes(req_seq);               return true; }
    if (cmd == "variables")  { handle_variables(s, req_seq, msg);   return true; }
    if (cmd == "evaluate")   { handle_evaluate(req_seq, msg);       return true; }
    if (cmd == "continue" || cmd == "next" || cmd == "stepIn" || cmd == "stepOut") {
        handle_continue(req_seq);
        return true;
    }
    if (cmd == "pause") {
        send_response(req_seq, "pause", true);
        send_event("stopped",
            "{\"reason\":\"pause\",\"threadId\":1,\"allThreadsStopped\":true}");
        return true;
    }
    if (cmd == "terminate")  { handle_terminate(s, req_seq); return false; }
    if (cmd == "disconnect") { handle_disconnect(s, req_seq); return false; }

    // Unknown request with seq — respond with "not supported"
    if (req_seq >= 0) {
        send_error_response(req_seq, cmd, "Command not supported: " + cmd);
    }
    return true;
}

}  // namespace

// ─── Entry point (declared in main.cpp) ───────────────────────────────────────

int run_dap_server(const char* exe_path) {
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    DapServer server;
    if (exe_path && exe_path[0] != '\0')
        server.exe_path = exe_path;

    std::cerr << "[dap] zephyr-dap server started\n";

    while (!server.terminated) {
        const std::string msg = dap_read_message();
        if (msg.empty()) break;
        if (!dap_dispatch(server, msg)) break;
    }

    if (server.exec_thread.joinable())
        server.exec_thread.join();

    std::cerr << "[dap] server exiting\n";
    return 0;
}
