#include "zephyr_internal.hpp"

namespace zephyr {

std::shared_ptr<BytecodeFunction> Runtime::compile_bytecode_function(const std::string& name, const std::vector<Param>& params, BlockStmt* body, const std::vector<std::string>& generic_params) {
    BytecodeCompiler compiler;
    return compiler.compile(name, params, body, false, generic_params);
}

RuntimeResult<std::shared_ptr<BytecodeFunction>> Runtime::compile_module_bytecode(const std::string& name, Program* program) {
    BytecodeCompiler compiler;
    return compiler.compile_module(name, program);
}

}  // namespace zephyr
