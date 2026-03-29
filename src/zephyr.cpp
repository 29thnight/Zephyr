#include "zephyr_internal.hpp"

namespace zephyr {

#include "zephyr_parser.inl"

RuntimeResult<std::unique_ptr<Program>> Runtime::parse_source(const std::string& source, const std::string& module_name) {
    Lexer lexer(source, module_name);
    ZEPHYR_TRY_ASSIGN(tokens, lexer.scan_tokens());
    Parser parser(std::move(tokens), module_name);
    return parser.parse_program();
}

std::shared_ptr<BytecodeFunction> Runtime::compile_bytecode_function(const std::string& name, const std::vector<Param>& params, BlockStmt* body, const std::vector<std::string>& generic_params) {
    BytecodeCompiler compiler;
    return compiler.compile(name, params, body, false, generic_params);
}

RuntimeResult<std::shared_ptr<BytecodeFunction>> Runtime::compile_module_bytecode(const std::string& name, Program* program) {
    BytecodeCompiler compiler;
    return compiler.compile_module(name, program);
}

}  // namespace zephyr
