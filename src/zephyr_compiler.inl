// Part of src/zephyr.cpp — included by zephyr.cpp
enum class BytecodeOp {
    LoadConst,
    LoadLocal,
    LoadUpvalue,
    LoadName,
    DefineLocal,
    DefineName,
    StoreLocal,
    StoreUpvalue,
    StoreName,
    Pop,
    Not,
    Negate,
    ToBool,
    Stringify,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    BuildArray,
    ArrayLength,
    LoadMember,
    StoreMember,
    LoadIndex,
    StoreIndex,
    Call,
    CallMember,
    Jump,
    JumpIfFalse,
    JumpIfFalsePop,
    JumpIfTrue,
    JumpIfNilKeep,
    SIAddStoreLocal,
    SILoadLocalLoadLocal,
    SILoadLocalAdd,
    SICmpJumpIfFalse,
    SILoadLocalAddStoreLocal,
    SILoadLocalConstAddStoreLocal,
    SILoadLocalLocalConstModulo,
    EnterScope,
    ExitScope,
    ImportModule,
    DeclareFunction,
    DeclareStruct,
    DeclareEnum,
    DeclareTrait,
    DeclareImpl,
    ExportName,
    BuildStruct,
    BuildEnum,
    BindPattern,
    IsEnumVariant,
    LoadEnumPayload,
    Fail,
    MatchFail,
    EvalAstExpr,
    ExecAstStmt,
    MakeFunction,
    MakeCoroutine,
    Resume,
    Yield,
    Return,
    R_ADD,
    R_SUB,
    R_MUL,
    R_DIV,
    R_MOD,
    R_LOAD_CONST,
    R_LOAD_GLOBAL,
    R_STORE_GLOBAL,
    R_MOVE,
    R_CALL,
    R_RETURN,
    R_JUMP,
    R_JUMP_IF_FALSE,
    R_JUMP_IF_TRUE,
    R_LT,
    R_LE,
    R_GT,
    R_GE,
    R_EQ,
    R_NE,
    R_NOT,
    R_NEG,
    R_YIELD,
    R_SI_ADD_STORE,
    R_SI_SUB_STORE,
    R_SI_MUL_STORE,
    R_SI_CMP_JUMP_FALSE,
    R_SI_LOAD_ADD_STORE,
};

struct BytecodeFunction;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
struct CompactInstruction {
    BytecodeOp op = BytecodeOp::LoadConst;
    union {
        std::int32_t operand;
        struct {
            std::uint8_t dst;
            std::uint8_t src1;
            std::uint8_t src2;
            std::uint8_t operand_a;
        };
    };
    std::uint32_t span_line = 1;
    mutable std::uint32_t ic_slot = std::numeric_limits<std::uint32_t>::max();
    mutable struct Shape* ic_shape = nullptr;

    CompactInstruction() : operand(0) {}
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static_assert(sizeof(CompactInstruction) <= 24, "CompactInstruction must stay hot and cache-friendly.");

struct InstructionMetadata {
    std::string string_operand;
    std::optional<std::string> type_name;
    std::vector<std::string> names;
    std::vector<std::int32_t> jump_table;
    Expr* expr = nullptr;
    Stmt* stmt = nullptr;
    Pattern* pattern = nullptr;
    std::shared_ptr<BytecodeFunction> bytecode;
    bool flag = false;
};

enum class SuperinstructionCompareKind : std::uint8_t {
    Less = 0,
    LessEqual = 1,
    Greater = 2,
    GreaterEqual = 3,
    Equal = 4,
    NotEqual = 5,
};

constexpr std::uint32_t kSICmpJumpKindShift = 29;
constexpr std::uint32_t kSICmpJumpTargetMask = (1u << kSICmpJumpKindShift) - 1u;

inline bool is_bytecode_comparison_op(BytecodeOp op) {
    return op == BytecodeOp::Less || op == BytecodeOp::LessEqual ||
           op == BytecodeOp::Greater || op == BytecodeOp::GreaterEqual ||
           op == BytecodeOp::Equal || op == BytecodeOp::NotEqual;
}

inline std::optional<SuperinstructionCompareKind> superinstruction_compare_kind(BytecodeOp op) {
    switch (op) {
        case BytecodeOp::Less: return SuperinstructionCompareKind::Less;
        case BytecodeOp::LessEqual: return SuperinstructionCompareKind::LessEqual;
        case BytecodeOp::Greater: return SuperinstructionCompareKind::Greater;
        case BytecodeOp::GreaterEqual: return SuperinstructionCompareKind::GreaterEqual;
        case BytecodeOp::Equal: return SuperinstructionCompareKind::Equal;
        case BytecodeOp::NotEqual: return SuperinstructionCompareKind::NotEqual;
        default: return std::nullopt;
    }
}

inline BytecodeOp bytecode_op_from_superinstruction_compare_kind(SuperinstructionCompareKind kind) {
    switch (kind) {
        case SuperinstructionCompareKind::Less: return BytecodeOp::Less;
        case SuperinstructionCompareKind::LessEqual: return BytecodeOp::LessEqual;
        case SuperinstructionCompareKind::Greater: return BytecodeOp::Greater;
        case SuperinstructionCompareKind::GreaterEqual: return BytecodeOp::GreaterEqual;
        case SuperinstructionCompareKind::Equal: return BytecodeOp::Equal;
        case SuperinstructionCompareKind::NotEqual: return BytecodeOp::NotEqual;
    }
    return BytecodeOp::Less;
}

inline bool try_pack_si_cmp_jump_operand(int jump_target, SuperinstructionCompareKind kind, int& packed_operand) {
    if (jump_target < 0 || static_cast<std::uint32_t>(jump_target) > kSICmpJumpTargetMask) {
        return false;
    }
    packed_operand = static_cast<int>((static_cast<std::uint32_t>(kind) << kSICmpJumpKindShift) |
                                      static_cast<std::uint32_t>(jump_target));
    return true;
}

inline int unpack_si_cmp_jump_target(int operand) {
    return static_cast<int>(static_cast<std::uint32_t>(operand) & kSICmpJumpTargetMask);
}

inline BytecodeOp unpack_si_cmp_jump_compare_op(int operand) {
    const auto kind = static_cast<SuperinstructionCompareKind>(
        (static_cast<std::uint32_t>(operand) >> kSICmpJumpKindShift) & 0x7u);
    return bytecode_op_from_superinstruction_compare_kind(kind);
}

inline bool try_pack_si_local_pair_operands(int first, int second, int& packed_operand) {
    if (first < 0 || second < 0 || first > 0xFFFF || second > 0xFFFF) {
        return false;
    }
    packed_operand = first | (second << 16);
    return true;
}

inline int unpack_si_local_pair_first(int operand) {
    return operand & 0xFFFF;
}

inline int unpack_si_local_pair_second(int operand) {
    return (static_cast<unsigned int>(operand) >> 16) & 0xFFFF;
}

inline bool try_pack_si_local_triple_operands(int first, int second, int destination, int& packed_operand) {
    constexpr int kBitsPerOperand = 10;
    constexpr int kMaxPackedOperand = (1 << kBitsPerOperand) - 1;
    if (first < 0 || second < 0 || destination < 0 ||
        first > kMaxPackedOperand || second > kMaxPackedOperand || destination > kMaxPackedOperand) {
        return false;
    }
    packed_operand = first | (second << kBitsPerOperand) | (destination << (kBitsPerOperand * 2));
    return true;
}

inline int unpack_si_local_triple_first(int operand) {
    return operand & 0x3FF;
}

inline int unpack_si_local_triple_second(int operand) {
    return (static_cast<unsigned int>(operand) >> 10) & 0x3FF;
}

inline int unpack_si_local_triple_destination(int operand) {
    return (static_cast<unsigned int>(operand) >> 20) & 0x3FF;
}

inline bool try_pack_si_local_const_local_operands(int source_local, int constant_index, int destination_local, int& packed_operand) {
    constexpr int kLocalBits = 10;
    constexpr int kConstantBits = 12;
    constexpr int kMaxLocal = (1 << kLocalBits) - 1;
    constexpr int kMaxConstant = (1 << kConstantBits) - 1;
    if (source_local < 0 || destination_local < 0 || constant_index < 0 ||
        source_local > kMaxLocal || destination_local > kMaxLocal || constant_index > kMaxConstant) {
        return false;
    }
    packed_operand = source_local |
                     (destination_local << kLocalBits) |
                     (constant_index << (kLocalBits * 2));
    return true;
}

inline int unpack_si_local_const_local_source(int operand) {
    return operand & 0x3FF;
}

inline int unpack_si_local_const_local_destination(int operand) {
    return (static_cast<unsigned int>(operand) >> 10) & 0x3FF;
}

inline int unpack_si_local_const_local_constant(int operand) {
    return (static_cast<unsigned int>(operand) >> 20) & 0xFFF;
}

inline bool try_pack_si_local_local_const_operands(int first_local, int second_local, int constant_index, int& packed_operand) {
    return try_pack_si_local_const_local_operands(first_local, constant_index, second_local, packed_operand);
}

inline int unpack_si_local_local_const_first(int operand) {
    return unpack_si_local_const_local_source(operand);
}

inline int unpack_si_local_local_const_second(int operand) {
    return unpack_si_local_const_local_destination(operand);
}

inline int unpack_si_local_local_const_constant(int operand) {
    return unpack_si_local_const_local_constant(operand);
}

inline bool try_pack_r_dst_index_operand(std::uint8_t dst, int index, int& packed_operand) {
    if (index < 0 || index > 0x00FFFFFF) {
        return false;
    }
    packed_operand = (index << 8) | static_cast<int>(dst);
    return true;
}

inline std::uint8_t unpack_r_dst_operand(int operand) {
    return static_cast<std::uint8_t>(operand & 0xFF);
}

inline int unpack_r_index_operand(int operand) {
    return static_cast<int>(static_cast<std::uint32_t>(operand) >> 8);
}

inline bool try_pack_r_src_index_operand(std::uint8_t src, int index, int& packed_operand) {
    if (index < 0 || index > 0x00FFFFFF) {
        return false;
    }
    packed_operand = (index << 8) | static_cast<int>(src);
    return true;
}

inline std::uint8_t unpack_r_src_operand(int operand) {
    return static_cast<std::uint8_t>(operand & 0xFF);
}

inline bool try_pack_r_cond_jump_operand(std::uint8_t src, int target, int& packed_operand) {
    return try_pack_r_src_index_operand(src, target, packed_operand);
}

inline int unpack_r_jump_target_operand(int operand) {
    return unpack_r_index_operand(operand);
}

inline bool is_register_comparison_op(BytecodeOp op) {
    return op == BytecodeOp::R_LT || op == BytecodeOp::R_LE ||
           op == BytecodeOp::R_GT || op == BytecodeOp::R_GE ||
           op == BytecodeOp::R_EQ || op == BytecodeOp::R_NE;
}

inline std::optional<SuperinstructionCompareKind> register_superinstruction_compare_kind(BytecodeOp op) {
    switch (op) {
        case BytecodeOp::R_LT: return SuperinstructionCompareKind::Less;
        case BytecodeOp::R_LE: return SuperinstructionCompareKind::LessEqual;
        case BytecodeOp::R_GT: return SuperinstructionCompareKind::Greater;
        case BytecodeOp::R_GE: return SuperinstructionCompareKind::GreaterEqual;
        case BytecodeOp::R_EQ: return SuperinstructionCompareKind::Equal;
        case BytecodeOp::R_NE: return SuperinstructionCompareKind::NotEqual;
        default: return std::nullopt;
    }
}

inline BytecodeOp register_bytecode_op_from_superinstruction_compare_kind(SuperinstructionCompareKind kind) {
    switch (kind) {
        case SuperinstructionCompareKind::Less: return BytecodeOp::R_LT;
        case SuperinstructionCompareKind::LessEqual: return BytecodeOp::R_LE;
        case SuperinstructionCompareKind::Greater: return BytecodeOp::R_GT;
        case SuperinstructionCompareKind::GreaterEqual: return BytecodeOp::R_GE;
        case SuperinstructionCompareKind::Equal: return BytecodeOp::R_EQ;
        case SuperinstructionCompareKind::NotEqual: return BytecodeOp::R_NE;
    }
    return BytecodeOp::R_LT;
}

inline int pack_r_si_cmp_jump_false_operand(std::uint8_t src1, std::uint8_t src2, SuperinstructionCompareKind kind) {
    return static_cast<int>(src1) |
           (static_cast<int>(src2) << 8) |
           (static_cast<int>(static_cast<std::uint8_t>(kind)) << 16);
}

inline std::uint8_t unpack_r_si_cmp_jump_false_src1(int operand) {
    return static_cast<std::uint8_t>(operand & 0xFF);
}

inline std::uint8_t unpack_r_si_cmp_jump_false_src2(int operand) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(operand) >> 8) & 0xFF);
}

inline BytecodeOp unpack_r_si_cmp_jump_false_compare_op(int operand) {
    const auto kind = static_cast<SuperinstructionCompareKind>((static_cast<std::uint32_t>(operand) >> 16) & 0xFF);
    return register_bytecode_op_from_superinstruction_compare_kind(kind);
}

inline bool try_pack_r_si_load_add_store_operand(std::uint8_t dst, std::uint8_t local_src, int constant_index, int& packed_operand) {
    if (constant_index < 0 || constant_index > 0xFFFF) {
        return false;
    }
    packed_operand = static_cast<int>(dst) |
                     (static_cast<int>(local_src) << 8) |
                     (constant_index << 16);
    return true;
}

inline std::uint8_t unpack_r_si_load_add_store_dst(int operand) {
    return static_cast<std::uint8_t>(operand & 0xFF);
}

inline std::uint8_t unpack_r_si_load_add_store_local_src(int operand) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(operand) >> 8) & 0xFF);
}

inline int unpack_r_si_load_add_store_constant(int operand) {
    return static_cast<int>((static_cast<std::uint32_t>(operand) >> 16) & 0xFFFF);
}

inline Span instruction_span(const CompactInstruction& instruction) {
    return Span{std::max<std::size_t>(1, static_cast<std::size_t>(instruction.span_line)), 1};
}

struct BytecodeFunction {
    std::string name;
    std::vector<CompactInstruction> instructions;
    std::vector<InstructionMetadata> metadata;
    std::vector<BytecodeConstant> constants;
    int local_count = 0;
    std::vector<std::string> local_names;
    std::vector<std::string> upvalue_names;
    std::vector<std::string> global_names;
    std::vector<std::size_t> line_table;
    std::vector<std::string> constant_descriptions;
    std::unordered_map<std::string, std::size_t> opcode_histogram;
    std::uint64_t superinstruction_fusions = 0;
    std::size_t total_original_opcode_count = 0;
    bool global_slots_use_module_root_base = true;
    bool requires_full_closure = false;
    bool uses_only_locals_and_upvalues = false;   // Phase 1.1: true when function uses only local slots + upvalue cells (no Environment chain needed)
    bool is_coroutine_body = false;
    bool uses_register_mode = false;
    int max_regs = 0;
};

class RegisterAllocator {
public:
    std::uint8_t alloc() { return alloc_temp(); }

    void free(std::uint8_t reg) { free_temp(reg); }

    std::uint8_t alloc_temp() {
        while (!free_temps_.empty()) {
            const std::uint8_t reg = free_temps_.back();
            free_temps_.pop_back();
            if (reg < pinned_limit_) {
                temp_regs_[reg] = false;
                continue;
            }
            temp_regs_[reg] = true;
            return reg;
        }
        if (next_reg > 255) {
            throw std::runtime_error("Register allocator exhausted 8-bit register space.");
        }
        const std::uint8_t reg = static_cast<std::uint8_t>(next_reg++);
        temp_regs_[reg] = true;
        max_regs = std::max(max_regs, next_reg);
        return reg;
    }

    std::uint8_t alloc_temp_block(std::size_t count) {
        if (count == 0) {
            return 0;
        }
        if (next_reg + static_cast<int>(count) > 256) {
            throw std::runtime_error("Register allocator exhausted 8-bit register space.");
        }
        const std::uint8_t start = static_cast<std::uint8_t>(next_reg);
        for (std::size_t index = 0; index < count; ++index) {
            temp_regs_[static_cast<std::size_t>(start) + index] = true;
        }
        next_reg += static_cast<int>(count);
        max_regs = std::max(max_regs, next_reg);
        return start;
    }

    std::uint8_t alloc_persistent() {
        if (next_reg > 255) {
            throw std::runtime_error("Register allocator exhausted 8-bit register space.");
        }
        const std::uint8_t reg = static_cast<std::uint8_t>(next_reg++);
        temp_regs_[reg] = false;
        max_regs = std::max(max_regs, next_reg);
        return reg;
    }

    void free_temp(std::uint8_t reg) {
        if (!is_temp(reg)) {
            return;
        }
        temp_regs_[reg] = false;
        if (reg < pinned_limit_) {
            return;
        }
        free_temps_.push_back(reg);
    }

    void free_temp_block(std::uint8_t start, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            free_temp(static_cast<std::uint8_t>(start + index));
        }
    }

    void reserve_pinned(int count) {
        pinned_limit_ = std::max(pinned_limit_, count);
        next_reg = std::max(next_reg, count);
        max_regs = std::max(max_regs, next_reg);
        free_temps_.erase(
            std::remove_if(free_temps_.begin(), free_temps_.end(), [&](std::uint8_t reg) { return reg < pinned_limit_; }),
            free_temps_.end());
    }

    bool is_temp(std::uint8_t reg) const {
        return static_cast<std::size_t>(reg) < temp_regs_.size() && temp_regs_[reg];
    }

    int next_reg = 0;
    int max_regs = 0;

private:
    int pinned_limit_ = 0;
    std::vector<std::uint8_t> free_temps_;
    std::vector<bool> temp_regs_ = std::vector<bool>(256, false);
};

inline std::string bytecode_op_name(BytecodeOp op) {
    switch (op) {
        case BytecodeOp::LoadConst: return "LoadConst";
        case BytecodeOp::LoadName: return "LoadName";
        case BytecodeOp::DefineName: return "DefineName";
        case BytecodeOp::StoreName: return "StoreName";
        case BytecodeOp::LoadLocal: return "LoadLocal";
        case BytecodeOp::LoadUpvalue: return "LoadUpvalue";
        case BytecodeOp::DefineLocal: return "DefineLocal";
        case BytecodeOp::StoreLocal: return "StoreLocal";
        case BytecodeOp::StoreUpvalue: return "StoreUpvalue";
        case BytecodeOp::Pop: return "Pop";
        case BytecodeOp::EnterScope: return "EnterScope";
        case BytecodeOp::ExitScope: return "ExitScope";
        case BytecodeOp::JumpIfFalse: return "JumpIfFalse";
        case BytecodeOp::JumpIfFalsePop: return "JumpIfFalsePop";
        case BytecodeOp::Jump: return "Jump";
        case BytecodeOp::Call: return "Call";
        case BytecodeOp::CallMember: return "CallMember";
        case BytecodeOp::Return: return "Return";
        case BytecodeOp::Yield: return "Yield";
        case BytecodeOp::SIAddStoreLocal: return "SIAddStoreLocal";
        case BytecodeOp::SILoadLocalLoadLocal: return "SILoadLocalLoadLocal";
        case BytecodeOp::SILoadLocalAdd: return "SILoadLocalAdd";
        case BytecodeOp::SICmpJumpIfFalse: return "SICmpJumpIfFalse";
        case BytecodeOp::SILoadLocalAddStoreLocal: return "SILoadLocalAddStoreLocal";
        case BytecodeOp::SILoadLocalConstAddStoreLocal: return "SILoadLocalConstAddStoreLocal";
        case BytecodeOp::SILoadLocalLocalConstModulo: return "SILoadLocalLocalConstModulo";
        case BytecodeOp::LoadMember: return "LoadMember";
        case BytecodeOp::StoreMember: return "StoreMember";
        case BytecodeOp::LoadIndex: return "LoadIndex";
        case BytecodeOp::StoreIndex: return "StoreIndex";
        case BytecodeOp::ToBool: return "ToBool";
        case BytecodeOp::Stringify: return "Stringify";
        case BytecodeOp::BuildArray: return "BuildArray";
        case BytecodeOp::JumpIfTrue: return "JumpIfTrue";
        case BytecodeOp::JumpIfNilKeep: return "JumpIfNilKeep";
        case BytecodeOp::BuildStruct: return "BuildStruct";
        case BytecodeOp::BuildEnum: return "BuildEnum";
        case BytecodeOp::ArrayLength: return "ArrayLength";
        case BytecodeOp::IsEnumVariant: return "IsEnumVariant";
        case BytecodeOp::LoadEnumPayload: return "LoadEnumPayload";
        case BytecodeOp::Equal: return "Equal";
        case BytecodeOp::NotEqual: return "NotEqual";
        case BytecodeOp::Less: return "Less";
        case BytecodeOp::LessEqual: return "LessEqual";
        case BytecodeOp::Greater: return "Greater";
        case BytecodeOp::GreaterEqual: return "GreaterEqual";
        case BytecodeOp::Add: return "Add";
        case BytecodeOp::Subtract: return "Subtract";
        case BytecodeOp::Multiply: return "Multiply";
        case BytecodeOp::Divide: return "Divide";
        case BytecodeOp::Modulo: return "Modulo";
        case BytecodeOp::Negate: return "Negate";
        case BytecodeOp::Not: return "Not";
        case BytecodeOp::ImportModule: return "ImportModule";
        case BytecodeOp::DeclareFunction: return "DeclareFunction";
        case BytecodeOp::DeclareStruct: return "DeclareStruct";
        case BytecodeOp::DeclareEnum: return "DeclareEnum";
        case BytecodeOp::DeclareTrait: return "DeclareTrait";
        case BytecodeOp::DeclareImpl: return "DeclareImpl";
        case BytecodeOp::ExportName: return "ExportName";
        case BytecodeOp::EvalAstExpr: return "EvalAstExpr";
        case BytecodeOp::ExecAstStmt: return "ExecAstStmt";
        case BytecodeOp::Fail: return "Fail";
        case BytecodeOp::MatchFail: return "MatchFail";
        case BytecodeOp::BindPattern: return "BindPattern";
        case BytecodeOp::MakeFunction: return "MakeFunction";
        case BytecodeOp::MakeCoroutine: return "MakeCoroutine";
        case BytecodeOp::Resume: return "Resume";
        case BytecodeOp::R_ADD: return "R_ADD";
        case BytecodeOp::R_SUB: return "R_SUB";
        case BytecodeOp::R_MUL: return "R_MUL";
        case BytecodeOp::R_DIV: return "R_DIV";
        case BytecodeOp::R_MOD: return "R_MOD";
        case BytecodeOp::R_LOAD_CONST: return "R_LOAD_CONST";
        case BytecodeOp::R_LOAD_GLOBAL: return "R_LOAD_GLOBAL";
        case BytecodeOp::R_STORE_GLOBAL: return "R_STORE_GLOBAL";
        case BytecodeOp::R_MOVE: return "R_MOVE";
        case BytecodeOp::R_CALL: return "R_CALL";
        case BytecodeOp::R_RETURN: return "R_RETURN";
        case BytecodeOp::R_JUMP: return "R_JUMP";
        case BytecodeOp::R_JUMP_IF_FALSE: return "R_JUMP_IF_FALSE";
        case BytecodeOp::R_JUMP_IF_TRUE: return "R_JUMP_IF_TRUE";
        case BytecodeOp::R_LT: return "R_LT";
        case BytecodeOp::R_LE: return "R_LE";
        case BytecodeOp::R_GT: return "R_GT";
        case BytecodeOp::R_GE: return "R_GE";
        case BytecodeOp::R_EQ: return "R_EQ";
        case BytecodeOp::R_NE: return "R_NE";
        case BytecodeOp::R_NOT: return "R_NOT";
        case BytecodeOp::R_NEG: return "R_NEG";
        case BytecodeOp::R_YIELD: return "R_YIELD";
        case BytecodeOp::R_SI_ADD_STORE: return "R_SI_ADD_STORE";
        case BytecodeOp::R_SI_SUB_STORE: return "R_SI_SUB_STORE";
        case BytecodeOp::R_SI_MUL_STORE: return "R_SI_MUL_STORE";
        case BytecodeOp::R_SI_CMP_JUMP_FALSE: return "R_SI_CMP_JUMP_FALSE";
        case BytecodeOp::R_SI_LOAD_ADD_STORE: return "R_SI_LOAD_ADD_STORE";
    }
    return "Unknown";
}

inline std::string describe_bytecode_constant_literal(const BytecodeConstant& constant) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "nil";
            } else if constexpr (std::is_same_v<T, bool>) {
                return value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(value);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;
                out << value;
                return out.str();
            } else {
                return value;
            }
        },
        constant);
}

struct ScriptFunctionObject final : GcObject {
    ScriptFunctionObject(std::string name, std::string module_name, std::vector<Param> params, std::optional<TypeRef> return_type, BlockStmt* body,
                         Environment* closure, Span definition_span, std::shared_ptr<BytecodeFunction> bytecode = {},
                         std::vector<std::string> generic_params = {})
        : GcObject(ObjectKind::ScriptFunction),
          name(std::move(name)),
          module_name(std::move(module_name)),
          params(std::move(params)),
          return_type(std::move(return_type)),
          body(body),
          closure(closure),
          definition_span(definition_span),
          bytecode(std::move(bytecode)),
          generic_params(std::move(generic_params)) {}

    void trace(class Runtime& runtime) override;

    std::string name;
    std::string module_name;
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
    BlockStmt* body = nullptr;
    Environment* closure = nullptr;
    Span definition_span;
    std::shared_ptr<BytecodeFunction> bytecode;
    std::vector<UpvalueCellObject*> captured_upvalues;
    std::vector<std::string> generic_params;  // Generic type parameters: <T, U, ...>
};

struct NativeFunctionObject final : GcObject {
    NativeFunctionObject(std::string name, ZephyrNativeFunction callback, std::vector<std::string> param_types,
                         std::optional<std::string> return_type)
        : GcObject(ObjectKind::NativeFunction),
          name(std::move(name)),
          callback(std::move(callback)),
          param_types(std::move(param_types)),
          return_type(std::move(return_type)) {}

    void trace(class Runtime&) override {}

    std::string name;
    ZephyrNativeFunction callback;
    std::vector<std::string> param_types;
    std::optional<std::string> return_type;
};

struct StructFieldSpec {
    std::string name;
    std::string type_name;
};

struct Shape {
    std::unordered_map<std::string, std::uint32_t> field_indices;
    std::uint32_t field_count = 0;

    static Shape* get_or_create(const std::vector<std::string>& field_names) {
        const std::string key = make_key(field_names);
        const auto cached = cache_.find(key);
        if (cached != cache_.end()) {
            return cached->second;
        }

        auto* shape = new Shape();
        shape->field_count = static_cast<std::uint32_t>(field_names.size());
        for (std::uint32_t index = 0; index < shape->field_count; ++index) {
            shape->field_indices.emplace(field_names[index], index);
        }
        cache_.emplace(key, shape);
        return shape;
    }

private:
    static std::string make_key(const std::vector<std::string>& field_names) {
        std::size_t total_size = field_names.empty() ? 0 : field_names.size() - 1;
        for (const auto& field_name : field_names) {
            total_size += field_name.size();
        }

        std::string key;
        key.reserve(total_size);
        for (std::size_t index = 0; index < field_names.size(); ++index) {
            if (index != 0) {
                key.push_back('\0');
            }
            key.append(field_names[index]);
        }
        return key;
    }

    inline static std::unordered_map<std::string, Shape*> cache_;
};

struct StructTypeObject final : GcObject {
    explicit StructTypeObject(std::string name) : GcObject(ObjectKind::StructType), name(std::move(name)) {}
    void trace(class Runtime&) override {}

    std::string name;
    std::vector<StructFieldSpec> fields;
};

struct StructInstanceObject final : GcObject {
    explicit StructInstanceObject(StructTypeObject* type) : GcObject(ObjectKind::StructInstance), type(type) {}
    void trace(class Runtime& runtime) override;

    StructTypeObject* type = nullptr;
    Shape* shape = nullptr;
    std::vector<Value> field_values;
    std::vector<std::uint64_t> remembered_cards;  // Phase 3.3: bitmap (1 bit = 1 card granule)

    std::size_t field_slot(const std::string& name) const {
        if (shape == nullptr) {
            return static_cast<std::size_t>(-1);
        }
        const auto it = shape->field_indices.find(name);
        if (it == shape->field_indices.end() || it->second >= field_values.size()) {
            return static_cast<std::size_t>(-1);
        }
        return static_cast<std::size_t>(it->second);
    }

    Value get_field(const std::string& name) const {
        const std::size_t slot = field_slot(name);
        return slot == static_cast<std::size_t>(-1) ? Value::nil() : field_values[slot];
    }

    void set_field(const std::string& name, Value value) {
        const std::size_t slot = field_slot(name);
        if (slot == static_cast<std::size_t>(-1)) {
            fail("Unknown struct field '" + name + "'.");
        }
        field_values[slot] = value;
    }

    bool has_field(const std::string& name) const {
        return field_slot(name) != static_cast<std::size_t>(-1);
    }
};

struct EnumVariantSpec {
    std::string name;
    std::vector<std::string> payload_types;
};

struct EnumTypeObject final : GcObject {
    explicit EnumTypeObject(std::string name) : GcObject(ObjectKind::EnumType), name(std::move(name)) {}
    void trace(class Runtime&) override {}

    std::string name;
    std::vector<EnumVariantSpec> variants;
};

struct EnumInstanceObject final : GcObject {
    EnumInstanceObject(EnumTypeObject* type, std::string variant)
        : GcObject(ObjectKind::EnumInstance), type(type), variant(std::move(variant)) {}
    void trace(class Runtime& runtime) override;

    EnumTypeObject* type = nullptr;
    std::string variant;
    std::vector<Value> payload;
    std::vector<std::uint64_t> remembered_cards;  // Phase 3.3: bitmap (1 bit = 1 card granule)
};

inline std::size_t struct_field_slot(const StructTypeObject* type, const std::string& field_name) {
    if (type == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto it = std::find_if(type->fields.begin(), type->fields.end(),
                                 [&](const StructFieldSpec& candidate) { return candidate.name == field_name; });
    if (it == type->fields.end()) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(std::distance(type->fields.begin(), it));
}

inline std::size_t ensure_environment_binding_slot(Environment* environment, const std::string& name) {
    if (environment == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto existing = environment->binding_slots.find(name);
    if (existing != environment->binding_slots.end()) {
        return existing->second;
    }
    const std::size_t slot = environment->binding_names.size();
    environment->binding_names.push_back(name);
    environment->binding_slots.emplace(name, slot);
    return slot;
}

struct CoroutineFrameState {
    std::string module_name;
    Environment* closure = nullptr;
    Environment* root_env = nullptr;
    Environment* current_env = nullptr;
    Environment* global_resolution_env = nullptr;
    std::shared_ptr<BytecodeFunction> bytecode;
    std::optional<std::string> return_type_name;
    std::vector<UpvalueCellObject*> captured_upvalues;
    std::vector<Environment*> scope_stack;
    std::vector<Value> stack;
    std::vector<Value> locals;
    std::vector<Environment*> local_binding_owners;
    std::vector<Binding*> local_bindings;
    std::vector<std::uint64_t> local_binding_versions;
    std::vector<Environment*> global_binding_owners;
    std::vector<Binding*> global_bindings;
    std::vector<std::uint64_t> global_binding_versions;
    std::vector<std::uint64_t> stack_cards;  // Phase 3.3: bitmap
    std::vector<std::uint64_t> local_cards;   // Phase 3.3: bitmap
    std::vector<Value> regs;
    std::vector<std::uint64_t> reg_cards;
    std::size_t ip = 0;
    std::size_t ip_index = 0;
    bool uses_register_mode = false;
    std::optional<std::size_t> pending_call_dst_reg;

    // Step 3: Small inline register file — avoids heap allocation for common
    // coroutines whose register count fits within kInlineRegs. Active when
    // reg_count > 0 and regs.empty(); ignored otherwise.
    static constexpr int kInlineRegs = 8;
    std::uint8_t reg_count = 0;
    Value inline_regs[kInlineRegs];
};

struct CoroutineObject final : GcObject {
    CoroutineObject(std::string module_name, Environment* closure, std::shared_ptr<BytecodeFunction> bytecode,
                    std::optional<std::string> return_type_name)
        : GcObject(ObjectKind::Coroutine) {
        CoroutineFrameState frame;
        frame.module_name = std::move(module_name);
        frame.closure = closure;
        frame.bytecode = std::move(bytecode);
        frame.return_type_name = std::move(return_type_name);
        frame.uses_register_mode = frame.bytecode != nullptr && frame.bytecode->uses_register_mode;
        frame.ip_index = 0;
        frames.push_back(std::move(frame));
    }
    void trace(class Runtime& runtime) override;

    std::vector<CoroutineFrameState> frames;
    bool started = false;
    bool suspended = false;
    bool completed = false;
    bool handle_retained = false;
    bool completion_traced = false;
    bool destroyed_traced = false;
    std::uint64_t trace_id = 0;
    std::size_t resume_count = 0;
    std::size_t yield_count = 0;
    std::size_t total_step_count = 0;
    std::size_t last_resume_step_count = 0;
    std::size_t max_resume_step_count = 0;
};

struct ModuleNamespaceObject final : GcObject {
    explicit ModuleNamespaceObject(std::string name, Environment* environment)
        : GcObject(ObjectKind::ModuleNamespace), name(std::move(name)), environment(environment) {}
    void trace(class Runtime& runtime) override;

    std::string name;
    Environment* environment = nullptr;
    std::vector<std::string> exports;
};

// ── Phase 0 sizeof baselines ──────────────────────────────────────────────────
// Records sizeof each GcObject subclass for OldSmallSpace size class planning.
// Phase 4A requires these values to define kSizeClasses[].
// Actual values on MSVC x64 will be confirmed at first compile (see build output).
// If any static_assert below fires, update the comment-value to match.
//
// GcObject base: sizeof(GcObject) = 48B on x64 (8B vtable + 4B kind + 4B pad + 32B GcHeader)
static_assert(sizeof(StringObject)          > sizeof(GcObject)); // ~80B
static_assert(sizeof(ArrayObject)           > sizeof(GcObject)); // ~96B
static_assert(sizeof(Environment)           > sizeof(GcObject)); // ~144B+
static_assert(sizeof(UpvalueCellObject)     > sizeof(GcObject)); // ~80B
static_assert(sizeof(ScriptFunctionObject)  > sizeof(GcObject)); // ~128B+
static_assert(sizeof(NativeFunctionObject)  > sizeof(GcObject)); // ~96B
static_assert(sizeof(StructTypeObject)      > sizeof(GcObject)); // ~80B
static_assert(sizeof(StructInstanceObject)  > sizeof(GcObject)); // ~112B
static_assert(sizeof(EnumTypeObject)        > sizeof(GcObject)); // ~80B
static_assert(sizeof(EnumInstanceObject)    > sizeof(GcObject)); // ~96B
static_assert(sizeof(CoroutineObject)       > sizeof(GcObject)); // ~80B
static_assert(sizeof(ModuleNamespaceObject) > sizeof(GcObject)); // ~80B
// ─────────────────────────────────────────────────────────────────────────────

struct ModuleRecord {
    std::string name;
    std::filesystem::path path;
    std::unique_ptr<Program> program;
    std::shared_ptr<BytecodeFunction> bytecode;
    Environment* environment = nullptr;
    ModuleNamespaceObject* namespace_object = nullptr;
    std::uint64_t file_mtime = 0;
    bool loaded = false;
    bool loading = false;
};

struct BytecodeCacheEntry {
    std::string source_path;
    std::uint64_t file_mtime = 0;
    std::vector<std::uint8_t> serialized_bytecode;
};

struct GCPauseRecord {
    std::uint64_t duration_ns = 0;
    bool is_full = false;
};

struct GCTraceEventRecord {
    GCTraceEvent::Type type = GCTraceEvent::Type::YoungStart;
    std::uint64_t timestamp_ns = 0;
    std::size_t heap_bytes_before = 0;
    std::size_t heap_bytes_after = 0;
};

struct HostModuleRecord {
    std::function<void(ZephyrModuleBinder&)> initializer;
};

namespace bytecode_cache {

constexpr std::uint32_t kFormatVersion = 1;

inline void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

inline void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

inline void append_i32(std::vector<std::uint8_t>& out, std::int32_t value) {
    append_u32(out, static_cast<std::uint32_t>(value));
}

inline void append_bool(std::vector<std::uint8_t>& out, bool value) {
    append_u8(out, value ? 1u : 0u);
}

inline void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

inline void append_string_vector(std::vector<std::uint8_t>& out, const std::vector<std::string>& values) {
    append_u32(out, static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        append_string(out, value);
    }
}

inline void append_i32_vector(std::vector<std::uint8_t>& out, const std::vector<std::int32_t>& values) {
    append_u32(out, static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) {
        append_i32(out, value);
    }
}

inline void append_size_vector(std::vector<std::uint8_t>& out, const std::vector<std::size_t>& values) {
    append_u32(out, static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) {
        append_u64(out, static_cast<std::uint64_t>(value));
    }
}

inline bool read_u8(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint8_t& value) {
    if (offset >= data.size()) {
        return false;
    }
    value = data[offset++];
    return true;
}

inline bool read_u32(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > data.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(data[offset++]) << shift;
    }
    return true;
}

inline bool read_u64(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > data.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(data[offset++]) << shift;
    }
    return true;
}

inline bool read_i32(const std::vector<std::uint8_t>& data, std::size_t& offset, std::int32_t& value) {
    std::uint32_t encoded = 0;
    if (!read_u32(data, offset, encoded)) {
        return false;
    }
    value = static_cast<std::int32_t>(encoded);
    return true;
}

inline bool read_bool(const std::vector<std::uint8_t>& data, std::size_t& offset, bool& value) {
    std::uint8_t encoded = 0;
    if (!read_u8(data, offset, encoded)) {
        return false;
    }
    value = encoded != 0;
    return true;
}

inline bool read_string(const std::vector<std::uint8_t>& data, std::size_t& offset, std::string& value) {
    std::uint32_t size = 0;
    if (!read_u32(data, offset, size) || offset + size > data.size()) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(data.data() + offset), size);
    offset += size;
    return true;
}

inline bool read_string_vector(const std::vector<std::uint8_t>& data, std::size_t& offset, std::vector<std::string>& values) {
    std::uint32_t count = 0;
    if (!read_u32(data, offset, count)) {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string value;
        if (!read_string(data, offset, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

inline bool read_i32_vector(const std::vector<std::uint8_t>& data, std::size_t& offset, std::vector<std::int32_t>& values) {
    std::uint32_t count = 0;
    if (!read_u32(data, offset, count)) {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t value = 0;
        if (!read_i32(data, offset, value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

inline bool read_size_vector(const std::vector<std::uint8_t>& data, std::size_t& offset, std::vector<std::size_t>& values) {
    std::uint32_t count = 0;
    if (!read_u32(data, offset, count)) {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t value = 0;
        if (!read_u64(data, offset, value)) {
            return false;
        }
        values.push_back(static_cast<std::size_t>(value));
    }
    return true;
}

inline void rebuild_opcode_histogram(BytecodeFunction& function) {
    function.opcode_histogram.clear();
    for (const auto& instruction : function.instructions) {
        ++function.opcode_histogram[bytecode_op_name(instruction.op)];
    }
}

inline void serialize_function(const BytecodeFunction& function, std::vector<std::uint8_t>& out) {
    append_string(out, function.name);
    append_u32(out, static_cast<std::uint32_t>(function.instructions.size()));
    for (const auto& instruction : function.instructions) {
        append_u32(out, static_cast<std::uint32_t>(instruction.op));
        append_i32(out, instruction.operand);
        append_u32(out, instruction.span_line);
    }

    append_u32(out, static_cast<std::uint32_t>(function.metadata.size()));
    for (const auto& metadata : function.metadata) {
        append_string(out, metadata.string_operand);
        append_bool(out, metadata.type_name.has_value());
        if (metadata.type_name.has_value()) {
            append_string(out, *metadata.type_name);
        }
        append_string_vector(out, metadata.names);
        append_i32_vector(out, metadata.jump_table);
        append_bool(out, metadata.flag);
        append_bool(out, metadata.bytecode != nullptr);
        if (metadata.bytecode != nullptr) {
            serialize_function(*metadata.bytecode, out);
        }
    }

    append_u32(out, static_cast<std::uint32_t>(function.constants.size()));
    for (const auto& constant : function.constants) {
        append_u8(out, static_cast<std::uint8_t>(constant.index()));
        switch (constant.index()) {
            case 0:
                break;
            case 1:
                append_bool(out, std::get<bool>(constant));
                break;
            case 2:
                append_u64(out, static_cast<std::uint64_t>(std::get<std::int64_t>(constant)));
                break;
            case 3:
                append_u64(out, std::bit_cast<std::uint64_t>(std::get<double>(constant)));
                break;
            case 4:
                append_string(out, std::get<std::string>(constant));
                break;
            default:
                break;
        }
    }

    append_i32(out, function.local_count);
    append_string_vector(out, function.local_names);
    append_string_vector(out, function.upvalue_names);
    append_string_vector(out, function.global_names);
    append_size_vector(out, function.line_table);
    append_string_vector(out, function.constant_descriptions);
    append_u64(out, function.superinstruction_fusions);
    append_u64(out, static_cast<std::uint64_t>(function.total_original_opcode_count));
    append_bool(out, function.global_slots_use_module_root_base);
    append_bool(out, function.requires_full_closure);
    append_bool(out, function.uses_only_locals_and_upvalues);
    append_bool(out, function.is_coroutine_body);
    append_bool(out, function.uses_register_mode);
    append_i32(out, function.max_regs);
}

inline bool deserialize_function(const std::vector<std::uint8_t>& data, std::size_t& offset, BytecodeFunction& function) {
    if (!read_string(data, offset, function.name)) {
        return false;
    }

    std::uint32_t instruction_count = 0;
    if (!read_u32(data, offset, instruction_count)) {
        return false;
    }
    function.instructions.clear();
    function.instructions.reserve(instruction_count);
    for (std::uint32_t i = 0; i < instruction_count; ++i) {
        std::uint32_t op = 0;
        CompactInstruction instruction;
        if (!read_u32(data, offset, op) ||
            !read_i32(data, offset, instruction.operand) ||
            !read_u32(data, offset, instruction.span_line)) {
            return false;
        }
        instruction.op = static_cast<BytecodeOp>(op);
        function.instructions.push_back(instruction);
    }

    std::uint32_t metadata_count = 0;
    if (!read_u32(data, offset, metadata_count)) {
        return false;
    }
    function.metadata.clear();
    function.metadata.reserve(metadata_count);
    for (std::uint32_t i = 0; i < metadata_count; ++i) {
        InstructionMetadata metadata;
        bool has_type_name = false;
        bool has_nested = false;
        if (!read_string(data, offset, metadata.string_operand) ||
            !read_bool(data, offset, has_type_name)) {
            return false;
        }
        if (has_type_name) {
            std::string type_name;
            if (!read_string(data, offset, type_name)) {
                return false;
            }
            metadata.type_name = std::move(type_name);
        }
        if (!read_string_vector(data, offset, metadata.names) ||
            !read_i32_vector(data, offset, metadata.jump_table) ||
            !read_bool(data, offset, metadata.flag) ||
            !read_bool(data, offset, has_nested)) {
            return false;
        }
        if (has_nested) {
            auto nested = std::make_shared<BytecodeFunction>();
            if (!deserialize_function(data, offset, *nested)) {
                return false;
            }
            metadata.bytecode = std::move(nested);
        }
        metadata.expr = nullptr;
        metadata.stmt = nullptr;
        metadata.pattern = nullptr;
        function.metadata.push_back(std::move(metadata));
    }

    std::uint32_t constant_count = 0;
    if (!read_u32(data, offset, constant_count)) {
        return false;
    }
    function.constants.clear();
    function.constants.reserve(constant_count);
    for (std::uint32_t i = 0; i < constant_count; ++i) {
        std::uint8_t index = 0;
        if (!read_u8(data, offset, index)) {
            return false;
        }
        switch (index) {
            case 0:
                function.constants.emplace_back(std::monostate{});
                break;
            case 1: {
                bool value = false;
                if (!read_bool(data, offset, value)) {
                    return false;
                }
                function.constants.emplace_back(value);
                break;
            }
            case 2: {
                std::uint64_t value = 0;
                if (!read_u64(data, offset, value)) {
                    return false;
                }
                function.constants.emplace_back(static_cast<std::int64_t>(value));
                break;
            }
            case 3: {
                std::uint64_t encoded = 0;
                if (!read_u64(data, offset, encoded)) {
                    return false;
                }
                function.constants.emplace_back(std::bit_cast<double>(encoded));
                break;
            }
            case 4: {
                std::string value;
                if (!read_string(data, offset, value)) {
                    return false;
                }
                function.constants.emplace_back(std::move(value));
                break;
            }
            default:
                return false;
        }
    }

    std::uint64_t total_original_opcode_count = 0;
    if (!read_i32(data, offset, function.local_count) ||
        !read_string_vector(data, offset, function.local_names) ||
        !read_string_vector(data, offset, function.upvalue_names) ||
        !read_string_vector(data, offset, function.global_names) ||
        !read_size_vector(data, offset, function.line_table) ||
        !read_string_vector(data, offset, function.constant_descriptions) ||
        !read_u64(data, offset, function.superinstruction_fusions) ||
        !read_u64(data, offset, total_original_opcode_count) ||
        !read_bool(data, offset, function.global_slots_use_module_root_base) ||
        !read_bool(data, offset, function.requires_full_closure) ||
        !read_bool(data, offset, function.uses_only_locals_and_upvalues) ||
        !read_bool(data, offset, function.is_coroutine_body) ||
        !read_bool(data, offset, function.uses_register_mode) ||
        !read_i32(data, offset, function.max_regs)) {
        return false;
    }
    function.total_original_opcode_count = static_cast<std::size_t>(total_original_opcode_count);
    rebuild_opcode_histogram(function);
    return true;
}

inline std::vector<std::uint8_t> serialize_module_bytecode(const BytecodeFunction& function) {
    std::vector<std::uint8_t> out;
    out.reserve(function.instructions.size() * 24);
    append_u32(out, kFormatVersion);
    serialize_function(function, out);
    return out;
}

inline std::shared_ptr<BytecodeFunction> deserialize_module_bytecode(const std::vector<std::uint8_t>& data) {
    std::size_t offset = 0;
    std::uint32_t version = 0;
    if (!read_u32(data, offset, version) || version != kFormatVersion) {
        return nullptr;
    }
    auto function = std::make_shared<BytecodeFunction>();
    if (!deserialize_function(data, offset, *function) || offset != data.size()) {
        return nullptr;
    }
    return function;
}

}  // namespace bytecode_cache

enum HostHandleFlags : std::uint8_t {
    HostHandleStrongResidencyBit = 1 << 0,
    HostHandleSerializableBit = 1 << 1,
    HostHandleInvalidBit = 1 << 2,
};

struct HostHandleEntry {
    std::uint32_t generation = 1;
    ZephyrHostHandleKind kind = ZephyrHostHandleKind::Generic;
    ZephyrHostHandleLifetime lifetime = ZephyrHostHandleLifetime::Persistent;
    std::uint8_t flags = 0;
    std::uint32_t policy_id = 0;
    std::uint32_t frame_epoch = 0;
    std::uint32_t tick_epoch = 0;
    std::uint32_t scene_epoch = 0;
    std::uint32_t runtime_slot = 0;
    ZephyrGuid128 stable_guid{};
    ZephyrHostHandlePolicy policy{};
    std::shared_ptr<ZephyrHostClass> host_class;
    std::weak_ptr<void> instance;
    std::shared_ptr<void> residency_owner;
    void* cached_instance = nullptr;
    std::string invalid_reason;

    bool invalid() const { return (flags & HostHandleInvalidBit) != 0; }
};

// ── Phase 1B: heap-space abstraction ──────────────────────────────────────
// HeapSpace is the pure-virtual contract that every logical heap region must
// satisfy.  In Phase 1B the only concrete implementation is LegacyHeapSpace,
// which wraps the pre-existing singly-linked objects_ list with zero
// behavioral change.  Subsequent phases add NurserySpace, OldSmallSpace, etc.

class HeapSpace {
public:
    virtual ~HeapSpace() = default;

    // Invoke `fn` for every live object owned by this space.
    virtual void for_each_object(const std::function<void(GcObject*)>& fn) = 0;

    // Bytes currently tracked as live within this space.
    // Returns 0 until the space begins independent live-byte accounting.
    virtual std::size_t live_bytes() const = 0;

    // Reset sweep cursor / per-space cycle state at the start of a GC cycle.
    // Color-reset of individual objects is handled by Runtime::begin_gc_cycle()
    // via for_each_object() after Phase 1C.
    virtual void begin_cycle() = 0;

    // Perform budgeted sweep work.  Returns when budget_work reaches 0 or
    // the space has no more white objects to reclaim.
    virtual void sweep(std::size_t& budget_work) = 0;

    // Validate internal invariants.  Returns "" on success, error string otherwise.
    virtual std::string verify() const = 0;

    // Phase 7 (compaction) hook.
    // Returns true if this space supports compaction (i.e., it can evacuate
    // live objects to a new layout and reclaim fragmented pages).
    // Default is false; OldSmallSpace and NurserySpace may override to true
    // when Phase 7 handle-indirection is in place.
    // LargeObjectSpace and PinnedSpace always return false.
    virtual bool can_compact() const noexcept { return false; }
};

// LegacyHeapSpace was removed in Phase 6.  All objects now route to one of:
//   NurserySpace (young), OldSmallSpace (old non-large), LargeObjectSpace, PinnedSpace.
// ── LargeObjectNode ───────────────────────────────────────────────────────────
// One node per object owned by LargeObjectSpace.  Allocated separately from the
// GcObject so no layout changes to GcHeader are required.  The LOS typically
// holds very few objects (size >= large_object_threshold), so O(n) node lookup
// in free_object() is acceptable.
struct LargeObjectNode {
    GcObject*        object     = nullptr;
    std::size_t      alloc_size = 0;   // == object->header.size_bytes at insert time
    LargeObjectNode* next       = nullptr;
    LargeObjectNode* prev       = nullptr;
};

// ── LargeObjectSpace ──────────────────────────────────────────────────────────
// Owns GcObjects whose size_bytes >= large_object_threshold.
// Objects are still allocated via the normal `new T()` path in Runtime::allocate;
// this class maintains the doubly-linked node list that enables independent
// sweep, live-byte tracking, and heap verification.
//
// Phase 2A: class is implemented but NOT registered in Runtime::all_spaces_.
//            All methods are dead code at this stage.
// Phase 2B: Runtime::allocate<T>() routes large objects here and &los_ is
//            pushed onto all_spaces_, activating the space.
//
// Sweep note (Phase 2A): White objects are deleted immediately.  Phase 2B will
// route GcFinalizableBit objects to Runtime::detach_queue_ before deletion by
// changing the sweep signature to accept a finalization callback.
class LargeObjectSpace final : public HeapSpace {
public:
    // ── Insertion / removal ───────────────────────────────────────────────────

    // Bind the Runtime's global live_bytes_ counter so that sweep/free_object
    // can keep it in sync with los_.live_bytes_.  Called once from Runtime ctor.
    void bind_live_bytes(std::size_t& global_ref) noexcept {
        global_live_bytes_ = &global_ref;
    }

    // Register an already-constructed GcObject with this space.
    // Called from Runtime::allocate<T>() for large objects (Phase 2B+).
    void insert(GcObject* object) {
        assert(object->header.space_kind == GcSpaceKind::LargeObject
            && "LargeObjectSpace::insert: space_kind must be set to LargeObject before insert");
        const std::size_t sz = object->header.size_bytes;
        auto* node = new LargeObjectNode{object, sz, head_, nullptr};
        if (head_ != nullptr) {
            head_->prev = node;
        } else {
            tail_ = node;
        }
        head_ = node;
        live_bytes_ += sz;
        // global_live_bytes_ is updated by Runtime::allocate<T>() itself.
    }

    // Unregister, unlink, and delete a LOS object.
    // Called from Runtime::process_detach_queue() (Phase 2B+).
    // Decrements both the local live_bytes_ and Runtime's global counter.
    void free_object(GcObject* object) {
        LargeObjectNode* node = find_node(object);
        if (node == nullptr) {
            return;  // defensive — should never happen if space is used correctly
        }
        live_bytes_ -= node->alloc_size;
        if (global_live_bytes_ != nullptr) {
            *global_live_bytes_ -= node->alloc_size;
        }
        unlink_node(node);
        delete node;
        delete object;
    }

    // True when the sweep cursor has exhausted the list for the current cycle.
    bool sweep_complete() const noexcept { return sweep_cursor_ == nullptr; }

    // Release all objects unconditionally (called from Runtime::~Runtime()).
    void destroy_all() noexcept {
        LargeObjectNode* n = head_;
        while (n != nullptr) {
            LargeObjectNode* next = n->next;
            delete n->object;
            delete n;
            n = next;
        }
        head_ = tail_ = sweep_cursor_ = nullptr;
        live_bytes_ = 0;
    }

    bool empty() const noexcept { return head_ == nullptr; }

    // ── HeapSpace interface ───────────────────────────────────────────────────

    void for_each_object(const std::function<void(GcObject*)>& fn) override {
        for (LargeObjectNode* n = head_; n != nullptr; n = n->next) {
            fn(n->object);
        }
    }

    std::size_t live_bytes() const override { return live_bytes_; }

    // Reset the sweep cursor to the head of the list.
    // Per-object color/flag reset is handled by Runtime::begin_gc_cycle()
    // via for_each_object() (Phase 1C pattern).
    void begin_cycle() override {
        sweep_cursor_ = head_;
    }

    // Sweep budgeted work: one budget unit per node visited.
    //   White  → object is unreachable → free immediately.
    //   !White → object survived → reset color to White for the next cycle.
    //
    // GcFinalizableBit: currently no object sets this bit, so we always free
    // directly.  When finalizers are introduced, route Finalizable LOS objects
    // to detach_queue_ (requires passing a detach callback here).
    void sweep(std::size_t& budget_work) override {
        while (sweep_cursor_ != nullptr && budget_work > 0) {
            --budget_work;
            LargeObjectNode* cur = sweep_cursor_;
            sweep_cursor_ = cur->next;
            GcObject* obj = cur->object;
            if (obj->header.color == GcColor::White) {
                live_bytes_ -= cur->alloc_size;
                if (global_live_bytes_ != nullptr) {
                    *global_live_bytes_ -= cur->alloc_size;
                }
                unlink_node(cur);
                delete cur;
                delete obj;
            } else {
                obj->header.color = GcColor::White;
            }
        }
    }

    std::string verify() const override {
        std::size_t computed_bytes = 0;
        const LargeObjectNode* n = head_;
        while (n != nullptr) {
            if (n->object == nullptr) {
                return "LargeObjectSpace::verify: null object pointer in node";
            }
            if (n->object->header.space_kind == GcSpaceKind::Uninitialized) {
                return "LargeObjectSpace::verify: Uninitialized space_kind (allocation path forgot to set it)";
            }
            if (n->object->header.space_kind != GcSpaceKind::LargeObject) {
                return "LargeObjectSpace::verify: object has wrong space_kind";
            }
            if ((n->object->header.flags & GcOldBit) == 0) {
                return "LargeObjectSpace::verify: LOS object missing GcOldBit";
            }
            if (n->alloc_size != n->object->header.size_bytes) {
                return "LargeObjectSpace::verify: node alloc_size != header.size_bytes";
            }
            computed_bytes += n->alloc_size;
            n = n->next;
        }
        if (computed_bytes != live_bytes_) {
            return "LargeObjectSpace::verify: live_bytes_ accounting drift";
        }
        return {};
    }

private:
    // Walk the list to find the node owning `object`.  O(n) — acceptable for LOS.
    LargeObjectNode* find_node(const GcObject* object) const noexcept {
        for (LargeObjectNode* n = head_; n != nullptr; n = n->next) {
            if (n->object == object) {
                return n;
            }
        }
        return nullptr;
    }

    void unlink_node(LargeObjectNode* node) noexcept {
        if (node->prev != nullptr) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        if (node->next != nullptr) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        node->next = node->prev = nullptr;
    }

    LargeObjectNode* head_          = nullptr;
    LargeObjectNode* tail_          = nullptr;
    LargeObjectNode* sweep_cursor_  = nullptr;
    std::size_t      live_bytes_    = 0;
    std::size_t*     global_live_bytes_ = nullptr;  // bound to Runtime::live_bytes_
};

// ── PinnedSpace ───────────────────────────────────────────────────────────────
// Owns GcObjects that are permanent roots: root_environment_, module environments,
// interned strings, and other objects that must survive every GC cycle.
//
// Key invariant: the GC sweep NEVER frees pinned objects.  sweep() is a no-op.
// Objects are removed only via explicit free_object() (e.g., module unload).
//
// Phase 3A: class is implemented but NOT registered in Runtime::all_spaces_.
//            All methods are dead code at this stage.
// Phase 3B: root_environment_ and module environments are allocated here, and
//            &pinned_ is pushed onto all_spaces_.
//
// Implementation note: a simple intrusive singly-linked list (via next_all) is
// used here.  A page-bump allocator can be introduced in Phase 3B+ if desired.
class PinnedSpace final : public HeapSpace {
public:
    // ── Insertion / removal ───────────────────────────────────────────────────

    // Prepend a constructed GcObject into the pinned list.
    // Called from Runtime::allocate_pinned<T>() (Phase 3B+).
    void insert(GcObject* object) noexcept {
        assert(object->header.space_kind == GcSpaceKind::Pinned
            && "PinnedSpace::insert: space_kind must be set to Pinned before insert");
        object->header.next_all = objects_;
        objects_ = object;
        live_bytes_ += object->header.size_bytes;
        // global_live_bytes_ is updated by Runtime::allocate<T>() itself.
    }

    // Remove and delete a pinned object.
    // Called when a permanent root is explicitly released (e.g., module unloaded).
    void free_object(GcObject* object) {
        GcObject** prev = &objects_;
        while (*prev != nullptr) {
            if (*prev == object) {
                *prev = object->header.next_all;
                object->header.next_all = nullptr;
                live_bytes_ -= object->header.size_bytes;
                if (global_live_bytes_ != nullptr) {
                    *global_live_bytes_ -= object->header.size_bytes;
                }
                delete object;
                return;
            }
            prev = &(*prev)->header.next_all;
        }
        // Defensive: object not found — no-op.
    }

    // Bind Runtime::live_bytes_ so free_object() keeps the global counter in sync.
    void bind_live_bytes(std::size_t& global_ref) noexcept {
        global_live_bytes_ = &global_ref;
    }

    // Release all pinned objects unconditionally (called from Runtime::~Runtime()).
    void destroy_all() noexcept {
        while (objects_ != nullptr) {
            GcObject* next = objects_->header.next_all;
            delete objects_;
            objects_ = next;
        }
        live_bytes_ = 0;
    }

    bool empty() const noexcept { return objects_ == nullptr; }

    // ── HeapSpace interface ───────────────────────────────────────────────────

    void for_each_object(const std::function<void(GcObject*)>& fn) override {
        for (GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            fn(obj);
        }
    }

    std::size_t live_bytes() const override { return live_bytes_; }

    // Pinned objects are permanent — no sweep cursor reset needed.
    void begin_cycle() override {}

    // Pinned objects are NEVER freed by the GC.
    // sweep() resets surviving objects' colors from Black/Gray back to White so
    // gc_verify_full() passes and the next cycle starts clean.
    // The pinned set is always small (root_env + module envs/namespaces), so this
    // is done non-budgeted (instant).
    void sweep(std::size_t& /*budget_work*/) override {
        for (GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            obj->header.color = GcColor::White;
        }
    }

    std::string verify() const override {
        std::size_t computed = 0;
        for (const GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            if (obj->header.space_kind == GcSpaceKind::Uninitialized) {
                return "PinnedSpace::verify: Uninitialized space_kind (allocation path forgot to set it)";
            }
            if (obj->header.space_kind != GcSpaceKind::Pinned) {
                return "PinnedSpace::verify: object has wrong space_kind (expected Pinned)";
            }
            if ((obj->header.flags & GcOldBit) == 0) {
                return "PinnedSpace::verify: pinned object missing GcOldBit";
            }
            computed += obj->header.size_bytes;
        }
        if (computed != live_bytes_) {
            return "PinnedSpace::verify: live_bytes_ accounting drift";
        }
        return {};
    }

private:
    GcObject*    objects_           = nullptr;
    std::size_t  live_bytes_        = 0;
    std::size_t* global_live_bytes_ = nullptr;  // bound to Runtime::live_bytes_
};

// ── OldSmallSpace slab infrastructure ─────────────────────────────────────────
//
// Phase 4A: Span + SizeClassList are implemented as dead code.
//           OldSmallSpace itself is implemented in LINKED-LIST mode so Phase 4B
//           can activate it without requiring slab allocation (which needs
//           placement-new and is deferred to Phase 4C).
//
// Phase 4B: OldSmallSpace is registered in all_spaces_; objects still use
//           new T() / delete.  Span/SizeClassList remain dormant.
//
// Phase 4C: allocate<T>() for old objects switches to placement-new into Span
//           slots; free_object() calls destructor + free_slot() instead of delete.
//
// ── Size-class table ─────────────────────────────────────────────────────────
// Each class covers objects whose sizeof is [prev_class+1 .. class] bytes.
// Values chosen to cover all GcObject subclasses with ≤12.5% waste.
// Refined by sizeof baselines block above; update here if any class overflows.
static constexpr std::size_t kOssClassCount = 10;
static constexpr std::size_t kOssSizeClasses[kOssClassCount] = {
     64,   // GcObject base + tiny payload
     96,   // StringObject, ArrayObject, StructTypeObject, EnumTypeObject, ModuleNamespaceObject
    128,   // NativeFunctionObject, StructInstanceObject, EnumInstanceObject, UpvalueCellObject
    192,   // ScriptFunctionObject, CoroutineObject (small)
    256,   // Environment (small), CoroutineObject (medium)
    384,   // Environment (medium), ScriptFunctionObject (large)
    512,   // Environment (large)
    768,
   1024,
   2048,  // max small-object size (< large_object_threshold = 4096)
};

// Returns the index of the smallest size class that fits `size`, or
// kOssClassCount if size is too large (should go to LOS).
[[nodiscard]] static constexpr std::size_t oss_class_for_size(std::size_t size) noexcept {
    for (std::size_t i = 0; i < kOssClassCount; ++i) {
        if (size <= kOssSizeClasses[i]) {
            return i;
        }
    }
    return kOssClassCount;  // no class fits — caller must route to LOS
}

// ── Span ─────────────────────────────────────────────────────────────────────
// A single 4096-byte page of fixed-size slots, plus metadata.
// The data page is allocated separately (aligned to kPageSize) so that
// `page_base = reinterpret_cast<uintptr_t>(obj) & ~(kPageSize-1)` works.
//
// Bitmap encoding: bit i = 1 means the slot is FREE / LIVE respectively.
// Max slots = kPageSize / min_slot_size = 4096 / 64 = 64 ≤ 128. Two uint64_t
// words (128 bits) are sufficient.
// Named GcSpan to avoid collision with the bytecode Span struct defined earlier.
struct GcSpan {
    static constexpr std::size_t kPageSize    = 4096;
    static constexpr std::size_t kBitmapWords = 2;   // 2 × 64 = 128 bit capacity

    // Allocate a new GcSpan for the given slot size.
    // Returns nullptr on allocation failure.
    [[nodiscard]] static GcSpan* create(std::size_t slot_sz) {
        void* page = ::operator new(kPageSize, std::align_val_t{kPageSize});
        if (page == nullptr) {
            return nullptr;
        }
        auto* span = new GcSpan{};
        span->page_base   = page;
        span->slot_size   = slot_sz;
        span->total_slots = kPageSize / slot_sz;
        // Mark all valid slots as free.
        for (std::size_t i = 0; i < span->total_slots; ++i) {
            span->free_bitmap[i / 64] |= (1ULL << (i % 64));
        }
        return span;
    }

    // Release the data page and the GcSpan metadata node.
    static void destroy(GcSpan* span) noexcept {
        if (span == nullptr) {
            return;
        }
        ::operator delete(span->page_base, std::align_val_t{kPageSize});
        delete span;
    }

    // Allocate the next free slot. Returns nullptr if full.
    [[nodiscard]] void* alloc_slot() noexcept {
        for (int w = 0; w < kBitmapWords; ++w) {
            if (free_bitmap[w] != 0) {
                const int bit = std::countr_zero(free_bitmap[w]);
                const auto idx = static_cast<std::size_t>(w * 64 + bit);
                if (idx >= total_slots) {
                    break;
                }
                free_bitmap[w] &= ~(1ULL << bit);
                live_bitmap[w] |=  (1ULL << bit);
                ++live_slots;
                return static_cast<char*>(page_base) + idx * slot_size;
            }
        }
        return nullptr;
    }

    // Mark a slot as free by pointer.
    void free_slot(const void* ptr) noexcept {
        const std::size_t idx  = slot_index(ptr);
        const std::size_t word = idx / 64;
        const std::size_t bit  = idx % 64;
        live_bitmap[word] &= ~(1ULL << bit);
        free_bitmap[word] |=  (1ULL << bit);
        --live_slots;
    }

    // Compute the slot index for a pointer that lies within this span's page.
    [[nodiscard]] std::size_t slot_index(const void* ptr) const noexcept {
        return (static_cast<const char*>(ptr) - static_cast<const char*>(page_base)) / slot_size;
    }

    // True if the slot at `ptr` is live (occupied).
    [[nodiscard]] bool slot_is_live(const void* ptr) const noexcept {
        const std::size_t idx  = slot_index(ptr);
        return (live_bitmap[idx / 64] >> (idx % 64)) & 1U;
    }

    [[nodiscard]] bool is_full()  const noexcept { return live_slots == total_slots; }
    [[nodiscard]] bool is_empty() const noexcept { return live_slots == 0; }

    void*       page_base   = nullptr;
    std::size_t slot_size   = 0;
    std::size_t total_slots = 0;
    std::size_t live_slots  = 0;

    std::uint64_t live_bitmap[kBitmapWords] = {};
    std::uint64_t free_bitmap[kBitmapWords] = {};

    GcSpan* next_span = nullptr;
    GcSpan* prev_span = nullptr;
};

// ── SizeClassList ─────────────────────────────────────────────────────────────
// Manages all spans for one size class. Spans are kept in three categories:
//   partial — has at least one free slot
//   full    — no free slots
//   empty   — no live slots (candidate for return to OS; deferred to Phase 4C)
struct SizeClassList {
    std::size_t slot_size = 0;

    // Allocate a slot. Pulls from a partial span or creates a new one.
    // page_map: OldSmallSpace's page→span index, updated here.
    [[nodiscard]] void* alloc_slot(std::unordered_map<void*, GcSpan*>& page_map) {
        // Prefer partial span.
        if (partial_head != nullptr) {
            GcSpan* span = partial_head;
            void* slot = span->alloc_slot();
            if (span->is_full()) {
                unlink_span(span, partial_head);
                prepend_span(span, full_head);
            }
            return slot;
        }
        // Try an empty span.
        if (empty_head != nullptr) {
            GcSpan* span = empty_head;
            unlink_span(span, empty_head);
            void* slot = span->alloc_slot();
            prepend_span(span, partial_head);
            return slot;
        }
        // Allocate a new span.
        GcSpan* span = GcSpan::create(slot_size);
        if (span == nullptr) {
            return nullptr;
        }
        page_map.emplace(span->page_base, span);
        void* slot = span->alloc_slot();
        if (span->is_full()) {
            prepend_span(span, full_head);
        } else {
            prepend_span(span, partial_head);
        }
        return slot;
    }

    // Free a slot. Moves span between categories as needed.
    void free_slot(GcSpan* span, const void* ptr) noexcept {
        const bool was_full = span->is_full();
        span->free_slot(ptr);
        if (was_full) {
            unlink_span(span, full_head);
            prepend_span(span, span->is_empty() ? empty_head : partial_head);
        } else if (span->is_empty()) {
            unlink_span(span, partial_head);
            prepend_span(span, empty_head);
        }
        // else remains partial — no category change
    }

    // Iterate every live slot (for for_each_object).
    void for_each_slot(const std::function<void(GcObject*)>& fn) {
        auto visit_list = [&](GcSpan* head) {
            for (GcSpan* span = head; span != nullptr; span = span->next_span) {
                for (std::size_t i = 0; i < span->total_slots; ++i) {
                    if ((span->live_bitmap[i / 64] >> (i % 64)) & 1U) {
                        auto* obj = reinterpret_cast<GcObject*>(
                            static_cast<char*>(span->page_base) + i * span->slot_size);
                        fn(obj);
                    }
                }
            }
        };
        visit_list(partial_head);
        visit_list(full_head);
    }

    // Sweep: free White objects, reset non-White to White.
    // live_bytes / global_live_bytes are decremented per freed object.
    void sweep(std::size_t& budget_work, std::size_t& local_live, std::size_t* global_live,
               std::unordered_map<void*, GcSpan*>& page_map) {
        auto sweep_list = [&](GcSpan*& head) {
            GcSpan* span = head;
            while (span != nullptr && budget_work > 0) {
                GcSpan* next = span->next_span;
                for (std::size_t i = 0; i < span->total_slots && budget_work > 0; ++i) {
                    if (!((span->live_bitmap[i / 64] >> (i % 64)) & 1U)) {
                        continue;
                    }
                    --budget_work;
                    auto* obj = reinterpret_cast<GcObject*>(
                        static_cast<char*>(span->page_base) + i * span->slot_size);
                    if (obj->header.color == GcColor::White) {
                        obj->~GcObject();
                        const bool was_full = span->is_full();
                        span->free_slot(obj);
                        local_live -= slot_size;
                        if (global_live != nullptr) {
                            *global_live -= slot_size;
                        }
                        if (was_full) {
                            unlink_span(span, full_head);
                            prepend_span(span, span->is_empty() ? empty_head : partial_head);
                            head = partial_head;  // re-anchor if needed
                        } else if (span->is_empty()) {
                            unlink_span(span, partial_head);
                            prepend_span(span, empty_head);
                        }
                    } else {
                        obj->header.color = GcColor::White;
                    }
                }
                span = next;
            }
        };
        sweep_list(full_head);
        sweep_list(partial_head);
        (void)page_map;
    }

    // Destroy all spans and their pages.
    void destroy_all(std::unordered_map<void*, GcSpan*>& page_map) noexcept {
        auto destroy_list = [&](GcSpan*& head) {
            while (head != nullptr) {
                GcSpan* next = head->next_span;
                page_map.erase(head->page_base);
                GcSpan::destroy(head);
                head = next;
            }
        };
        destroy_list(partial_head);
        destroy_list(full_head);
        destroy_list(empty_head);
    }

    GcSpan* partial_head = nullptr;
    GcSpan* full_head    = nullptr;
    GcSpan* empty_head   = nullptr;

private:
    static void unlink_span(GcSpan* span, GcSpan*& head) noexcept {
        if (span->prev_span != nullptr) {
            span->prev_span->next_span = span->next_span;
        } else {
            head = span->next_span;
        }
        if (span->next_span != nullptr) {
            span->next_span->prev_span = span->prev_span;
        }
        span->next_span = span->prev_span = nullptr;
    }

    static void prepend_span(GcSpan* span, GcSpan*& head) noexcept {
        span->next_span = head;
        span->prev_span = nullptr;
        if (head != nullptr) {
            head->prev_span = span;
        }
        head = span;
    }
};

// ── OldSmallSpace ─────────────────────────────────────────────────────────────
// Owns non-large, non-pinned old-generation objects.
//
// Phase 4A/4B: objects use new T() / delete (linked-list mode like LegacyHeapSpace).
//              Span/SizeClassList members are present but unused.
// Phase 4C: slab allocation (placement-new into Span slots) replaces new/delete.
//
// NOT registered in all_spaces_ until Phase 4B.
class OldSmallSpace final : public HeapSpace {
public:
    // Phase 5C: callback invoked when a promoted bump object is freed during
    // full GC sweep.  Set by Runtime to call nursery_.release_promoted_slot().
    std::function<void(GcObject*)> bump_release_callback_;

    // ── Linked-list interface (Phase 4B active path) ──────────────────────────

    void insert(GcObject* object) noexcept {
        assert(object->header.space_kind == GcSpaceKind::OldSmall
            && "OldSmallSpace::insert: space_kind must be set to OldSmall before insert");
        object->header.next_all = objects_;
        objects_ = object;
        live_bytes_ += object->header.size_bytes;
    }

    void free_object(GcObject* object) {
        // Linked-list (new/delete) path only. Slab objects carry GcSlabBit and
        // must be freed via free_slab_object() — mixing the two is a bug.
        assert((object->header.flags & GcSlabBit) == 0
               && "free_object called on a slab-allocated object — "
                  "use free_slab_object() for objects with GcSlabBit set");
        remove_from_list(object);
        live_bytes_ -= object->header.size_bytes;
        if (global_live_bytes_ != nullptr) {
            *global_live_bytes_ -= object->header.size_bytes;
        }
        delete object;
    }

    // Phase 5C: free a promoted bump object.  Unlinks from list, decrements
    // counters, calls destructor (no delete), invokes bump_release_callback_.
    void free_bump_object(GcObject* object) {
        remove_from_list(object);
        live_bytes_ -= object->header.size_bytes;
        if (global_live_bytes_ != nullptr) {
            *global_live_bytes_ -= object->header.size_bytes;
        }
        object->~GcObject();
        if (bump_release_callback_) bump_release_callback_(object);
    }

    // Unlink without deleting (used by promote_object in some paths).
    void remove(GcObject* object) noexcept {
        remove_from_list(object);
        live_bytes_ -= object->header.size_bytes;
    }

    void bind_live_bytes(std::size_t& global_ref) noexcept {
        global_live_bytes_ = &global_ref;
    }

    void destroy_all() noexcept {
        // Walk linked list: for slab objects, call destructor only (the backing
        // slab memory is owned by the spans, freed below). For bump-allocated
        // objects promoted from nursery, call destructor only (the chunk pages
        // are owned by NurserySpace and freed by ~NurserySpace() after this
        // function returns). For heap objects, use delete as before.
        while (objects_ != nullptr) {
            GcObject* next = objects_->header.next_all;
            live_bytes_ -= objects_->header.size_bytes;
            if ((objects_->header.flags & (GcSlabBit | GcBumpAllocBit)) != 0) {
                objects_->~GcObject();  // destructor only; memory freed elsewhere
            } else {
                delete objects_;
            }
            objects_ = next;
        }
        // Free all slab span backing pages (gated on slab_active_).
        if (slab_active_) {
            for (std::size_t i = 0; i < kOssClassCount; ++i) {
                size_class_lists_[i].destroy_all(page_to_span_);
            }
            page_to_span_.clear();
        }
        live_bytes_ = 0;
    }

    bool empty() const noexcept { return objects_ == nullptr; }

    // ── Slab interface (Phase 4C active path — dormant until then) ────────────

    // Returns the slab slot size that would be used for a request of `size` bytes,
    // or 0 if no class fits (i.e., the object would go to LOS).
    [[nodiscard]] static constexpr std::size_t slot_size_for(std::size_t size) noexcept {
        const std::size_t ci = oss_class_for_size(size);
        return (ci < kOssClassCount) ? kOssSizeClasses[ci] : 0;
    }

    // Try to allocate a slot of the given size from the slab.
    // Returns nullptr if size exceeds all classes (caller routes to LOS).
    // On the first successful allocation, sets slab_active_ = true so that
    // destroy_all() and free_slab_object() know slab spans exist.
    [[nodiscard]] void* try_alloc_slab(std::size_t size) {
        const std::size_t ci = oss_class_for_size(size);
        if (ci >= kOssClassCount) {
            return nullptr;
        }
        size_class_lists_[ci].slot_size = kOssSizeClasses[ci];
        void* slot = size_class_lists_[ci].alloc_slot(page_to_span_);
        if (slot != nullptr) {
            slab_active_ = true;  // lazy activation: set true on first successful alloc
        }
        return slot;
    }

    // Free an object that was placement-new'd into a slab slot.
    void free_slab_object(GcObject* object) noexcept {
        assert(slab_active_ && "free_slab_object called but slab is not active");
        const auto key = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(object) & ~(GcSpan::kPageSize - 1));
        auto it = page_to_span_.find(key);
        if (it == page_to_span_.end()) {
            return;  // not a slab object — defensive
        }
        GcSpan* span = it->second;
        const std::size_t ci = oss_class_for_size(span->slot_size);
        object->~GcObject();
        if (ci < kOssClassCount) {
            size_class_lists_[ci].free_slot(span, object);
        }
        live_bytes_ -= span->slot_size;
        if (global_live_bytes_ != nullptr) {
            *global_live_bytes_ -= span->slot_size;
        }
    }

    // ── HeapSpace interface ───────────────────────────────────────────────────

    void for_each_object(const std::function<void(GcObject*)>& fn) override {
        // Iterate linked list: all objects (slab and non-slab alike) are
        // tracked here via GcHeader::next_all. Slab objects carry GcSlabBit
        // so callers can distinguish them from new/delete objects.
        // NOTE: we intentionally do NOT iterate slab bitmaps separately —
        // slab objects appear in the linked list and iterating the bitmaps
        // would produce double-visits, corrupting GC mark state and verification.
        for (GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            fn(obj);
        }
    }

    std::size_t live_bytes() const override { return live_bytes_; }

    // Phase 7: adjust live_bytes_ by a signed delta (e.g. when a bump object
    // is compacted into a larger slab slot).
    void adjust_live_bytes(std::int64_t delta) noexcept {
        live_bytes_ = static_cast<std::size_t>(static_cast<std::int64_t>(live_bytes_) + delta);
    }

    // Phase 4C: bytes committed in slab pages (reserved) and bytes actually
    // occupied by live slots (used = live_slots * slot_size per span).
    // used_bytes >= live_bytes_ because slot_size >= sizeof(T).
    std::size_t slab_reserved_bytes() const noexcept {
        if (!slab_active_) { return 0; }
        std::size_t total = 0;
        auto count_list = [&](const GcSpan* head) {
            for (const GcSpan* s = head; s != nullptr; s = s->next_span) {
                total += GcSpan::kPageSize;
            }
        };
        for (std::size_t i = 0; i < kOssClassCount; ++i) {
            count_list(size_class_lists_[i].partial_head);
            count_list(size_class_lists_[i].full_head);
            count_list(size_class_lists_[i].empty_head);
        }
        return total;
    }

    std::size_t slab_used_bytes() const noexcept {
        if (!slab_active_) { return 0; }
        std::size_t total = 0;
        auto count_list = [&](const GcSpan* head, std::size_t slot_sz) {
            for (const GcSpan* s = head; s != nullptr; s = s->next_span) {
                total += s->live_slots * slot_sz;
            }
        };
        for (std::size_t i = 0; i < kOssClassCount; ++i) {
            const std::size_t sz = kOssSizeClasses[i];
            count_list(size_class_lists_[i].partial_head, sz);
            count_list(size_class_lists_[i].full_head,    sz);
        }
        return total;
    }

    void begin_cycle() override {
        // Phase 4B: reset sweep cursor for linked-list sweep.
        sweep_cursor_     = nullptr;
        sweep_previous_   = nullptr;
        sweep_chain_head_ = nullptr;
    }

    // Called from Runtime::process_dirty_roots() when entering SweepObjects.
    // Detaches the existing object chain from new allocations:
    //   - sweep_cursor_ walks the frozen pre-transition chain.
    //   - objects_ is reset to nullptr so new post-transition allocations
    //     build a separate chain that sweep() will never touch.
    // On sweep completion, sweep() splices survivors back onto objects_.
    void begin_sweep() noexcept {
        sweep_cursor_     = objects_;
        sweep_previous_   = nullptr;
        sweep_chain_head_ = nullptr;
        objects_          = nullptr;  // isolate new allocs from sweep chain
    }

    // Phase 4B: sweep linked-list (same pattern as LegacyHeapSpace).
    // Phase 4C: slab sweep via SizeClassList::sweep().
    void sweep(std::size_t& budget_work) override {
        // ── Linked-list sweep (Phase 4B) ─────────────────────────────────────
        // sweep_cursor_ was frozen by begin_sweep(); objects_ holds only post-
        // transition allocations. We never write to objects_ while sweeping —
        // doing so could orphan objects prepended between GC steps.
        while (sweep_cursor_ != nullptr && budget_work > 0) {
            GcObject* current = sweep_cursor_;
            GcObject* next    = current->header.next_all;
            if (current->header.color == GcColor::White) {
                // Unlink from the sweep chain.
                if (sweep_previous_ != nullptr) {
                    sweep_previous_->header.next_all = next;
                }
                // else: current was the sweep chain head — no head pointer to
                // update here; sweep_chain_head_ has not been set yet (no
                // survivor before this), so we simply drop current and continue.
                live_bytes_ -= current->header.size_bytes;
                if (global_live_bytes_ != nullptr) {
                    *global_live_bytes_ -= current->header.size_bytes;
                }
                // Phase 4C: slab objects carry GcSlabBit and were placement-new'd
                // into a slab slot; free via slab machinery. Linked-list objects
                // were allocated with new T() and must be freed with delete.
                if ((current->header.flags & GcSlabBit) != 0) {
                    // live_bytes_ and *global_live_bytes_ already decremented above;
                    // free_slab_object would decrement again — call destructor +
                    // return slot directly without the accounting side-effects.
                    const auto key = reinterpret_cast<void*>(
                        reinterpret_cast<uintptr_t>(current) & ~(GcSpan::kPageSize - 1));
                    auto it = page_to_span_.find(key);
                    if (it != page_to_span_.end()) {
                        GcSpan* span = it->second;
                        const std::size_t ci = oss_class_for_size(span->slot_size);
                        current->~GcObject();
                        if (ci < kOssClassCount) {
                            size_class_lists_[ci].free_slot(span, current);
                        }
                    }
                } else if ((current->header.flags & GcBumpAllocBit) != 0) {
                    // Phase 5C: promoted bump object — destructor only; chunk
                    // memory is owned by NurserySpace.  The chunk's promoted
                    // counter is decremented via bump_release_callback_ below.
                    current->~GcObject();
                    if (bump_release_callback_) bump_release_callback_(current);
                } else {
                    delete current;
                }
            } else {
                current->header.color = GcColor::White;
                if (sweep_chain_head_ == nullptr) {
                    sweep_chain_head_ = current;  // first survivor in swept chain
                }
                sweep_previous_ = current;
            }
            sweep_cursor_ = next;
            --budget_work;
        }
        // Splice: when the full sweep chain is consumed and survivors exist,
        // prepend the survivor chain onto the new-allocation chain.
        // Guard: sweep_chain_head_ != nullptr  ←→  splice not yet performed.
        // After splice both sweep_chain_head_ and sweep_previous_ are cleared so
        // that subsequent sweep() calls (sweep_cursor_ already null) are no-ops
        // and cannot re-execute the splice, which would create a list cycle.
        // If there are no survivors, objects_ already holds the new-alloc chain.
        if (sweep_cursor_ == nullptr && sweep_chain_head_ != nullptr) {
            sweep_previous_->header.next_all = objects_;   // tail → new allocs
            objects_          = sweep_chain_head_;         // head of merged list
            sweep_chain_head_ = nullptr;
            sweep_previous_   = nullptr;
        }
        // ── Slab sweep (disabled in Phase 4C) ───────────────────────────────
        // In Phase 4C slab objects are tracked AND swept via the linked list
        // (they carry GcSlabBit so OldSmallSpace::sweep() calls ~GcObject() +
        // free_slot() for White slab objects, and resets Black ones to White).
        // SizeClassList::sweep() iterates live bitmap slots and would see the
        // already-White survivors as "dead", calling their destructors and
        // freeing the slots a second time — causing use-after-free.
        // Enable SizeClassList::sweep() only after slab objects are removed
        // from the linked list (a future "bitmap-only" tracking phase).
        (void)slab_active_;  // suppress unused-variable warning
    }

    [[nodiscard]] bool sweep_complete() const noexcept {
        return sweep_cursor_ == nullptr;
    }

    // Phase 7 (compaction): walk the objects_ linked list and replace any
    // forwarded node (old location, color == Gray) with its forwarded target
    // (new location, stored in next_gray).  This rebuilds the list in O(n)
    // without requiring per-node removal.
    void fixup_linked_list_for_compaction() noexcept {
        GcObject* new_head = nullptr;
        GcObject* new_tail = nullptr;
        GcObject* current  = objects_;
        while (current != nullptr) {
            GcObject* next = current->header.next_all;
            GcObject* actual = current;
            // If this node was forwarded, use the new location instead.
            if (current->header.color == GcColor::Gray &&
                current->header.next_gray != nullptr) {
                actual = current->header.next_gray;
            }
            actual->header.next_all = nullptr;
            if (new_tail != nullptr) {
                new_tail->header.next_all = actual;
            } else {
                new_head = actual;
            }
            new_tail = actual;
            current = next;
        }
        objects_ = new_head;
    }

    std::string verify() const override {
        std::size_t computed = 0;
        for (const GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            if (obj->header.space_kind == GcSpaceKind::Uninitialized) {
                return "OldSmallSpace::verify: Uninitialized space_kind (allocation path forgot to set it)";
            }
            if (obj->header.space_kind != GcSpaceKind::OldSmall) {
                return "OldSmallSpace::verify: object has wrong space_kind";
            }
            if ((obj->header.flags & GcOldBit) == 0) {
                return "OldSmallSpace::verify: old object missing GcOldBit";
            }
            // Phase 4C: cross-check slab objects against the page_to_span_ index.
            if ((obj->header.flags & GcSlabBit) != 0) {
                const auto key = reinterpret_cast<const void*>(
                    reinterpret_cast<uintptr_t>(obj) & ~(GcSpan::kPageSize - 1));
                auto it = page_to_span_.find(const_cast<void*>(key));
                if (it == page_to_span_.end()) {
                    return "OldSmallSpace::verify: slab object has no page_to_span_ entry";
                }
                const GcSpan* span = it->second;
                if (!span->slot_is_live(obj)) {
                    return "OldSmallSpace::verify: slab object's slot not marked live in bitmap";
                }
                if (obj->header.size_bytes != span->slot_size) {
                    return "OldSmallSpace::verify: slab object size_bytes != span->slot_size";
                }
            }
            computed += obj->header.size_bytes;
        }
        if (computed != live_bytes_) {
            return "OldSmallSpace::verify: live_bytes_ accounting drift";
        }
        return {};
    }

private:
    void remove_from_list(GcObject* object) noexcept {
        GcObject** prev = &objects_;
        while (*prev != nullptr) {
            if (*prev == object) {
                *prev = object->header.next_all;
                object->header.next_all = nullptr;
                return;
            }
            prev = &(*prev)->header.next_all;
        }
    }

    // ── Linked-list state (Phase 4B) ─────────────────────────────────────────
    GcObject*   objects_           = nullptr;
    GcObject*   sweep_cursor_      = nullptr;
    GcObject*   sweep_previous_    = nullptr;
    // Head of the surviving-objects chain built during an incremental sweep.
    // begin_sweep() detaches the sweep chain from objects_ (sets objects_ = nullptr
    // so new allocations form a separate prefix chain). sweep() accumulates
    // survivors here. When sweep_cursor_ reaches nullptr, the survivor chain is
    // spliced back onto the front of objects_ (which now holds only post-begin_sweep
    // allocations). This prevents new allocations from being orphaned when the
    // first swept object is White and would otherwise trigger objects_ = next.
    GcObject*   sweep_chain_head_  = nullptr;
    std::size_t live_bytes_        = 0;
    std::size_t* global_live_bytes_ = nullptr;

    // ── Slab state (Phase 4C) ─────────────────────────────────────────────────
    // slab_active_ is set to true in 4C-1 when try_alloc_slab() is first wired
    // into allocate<T>(). Until then, slab loops are skipped and free_slab_object
    // / free_object asserts enforce single-path invariant.
    bool slab_active_ = false;
    SizeClassList size_class_lists_[kOssClassCount];
    std::unordered_map<void*, GcSpan*> page_to_span_;
};

// ──────────────────────────────────────────────────────────────────────────────
// NurserySpace — young-generation space (Phase 5C: bump-pointer allocation).
//
// Young allocations (non-old, non-large, non-pinned) route here.
// Fast path: objects are placement-new'd into 256 KiB NurseryChunk arenas.
//   These objects carry GcBumpAllocBit and must NOT be deleted with `delete`;
//   their memory is owned by the chunk and reclaimed wholesale after young
//   collection (when all survivors have been promoted to OldSmallSpace).
// Fallback: oversized objects (> chunk capacity) use new T() / delete and are
//   linked into objects_ without GcBumpAllocBit (legacy path).
// GC traversal uses the objects_ linked list for both bump and fallback objects.
// After young collection: nursery_.objects_ is always nullptr (all promoted or
//   freed), making chunk reset safe.
// ──────────────────────────────────────────────────────────────────────────────

// Single 256 KiB arena used for bump-pointer nursery allocation.
// Layout: [NurseryChunkHeader | padding | obj0 | obj1 | ...]
//                              ^--- bump_ptr starts here (kChunkDataOffset)
struct NurseryChunkHeader {
    NurseryChunkHeader* next_chunk;   // intrusive free/active list
    char*               bump_ptr;    // next free byte (aligned on entry)
    char*               limit;       // one past last usable byte
    std::size_t         promoted_count = 0;  // # of live promoted objects in this chunk
};

// 256 KiB per chunk; kChunkDataOffset aligns the first object slot to
// alignof(std::max_align_t) so placement-new of any GcObject subtype is safe.
static constexpr std::size_t kNurseryChunkSize =
    256 * 1024;
static constexpr std::size_t kNurseryChunkDataOffset =
    (sizeof(NurseryChunkHeader) + alignof(std::max_align_t) - 1)
    & ~(alignof(std::max_align_t) - 1);

class NurserySpace final : public HeapSpace {
public:
    // Public for direct sweep manipulation in Runtime::sweep_young_objects().
    GcObject*   objects_           = nullptr;
    GcObject*   sweep_cursor_      = nullptr;
    GcObject*   sweep_previous_    = nullptr;
    // See OldSmallSpace::sweep_chain_head_ for the full explanation.
    // sweep_young_objects() runs only at Idle (non-incremental) and does not
    // use the sweep cursor machinery, so this field is irrelevant to it.
    GcObject*   sweep_chain_head_  = nullptr;
    std::size_t live_bytes_own_    = 0;  // per-space counter

    // ── Phase 5C: bump-pointer chunk pool ────────────────────────────────────
    // current_chunk_: active allocation arena (bump_ptr moves forward).
    // free_chunk_list_: recycled empty arenas waiting for the next young cycle.
    // Both pointers are nullptr until the first nursery bump allocation.
    NurseryChunkHeader* current_chunk_    = nullptr;
    NurseryChunkHeader* free_chunk_list_  = nullptr;
    NurseryChunkHeader* retained_chunks_  = nullptr;  // chunks pinned by promoted objects

    void insert(GcObject* object) noexcept {
        assert(object->header.space_kind == GcSpaceKind::Nursery
            && "NurserySpace::insert: space_kind must be set to Nursery before insert");
        object->header.next_all = objects_;
        objects_ = object;
        live_bytes_own_ += object->header.size_bytes;
    }

    // Unlink without deleting — O(n) walk.
    // NOTE: sweep_young_objects() now unlinks directly in O(1) before calling
    // promote_object(). This method is retained for any future callers that
    // don't have a `previous` pointer handy.
    void remove(GcObject* object) noexcept {
        remove_from_list(object);
        live_bytes_own_ -= object->header.size_bytes;
    }

    void bind_live_bytes(std::size_t& global_ref) noexcept {
        global_live_bytes_ = &global_ref;
    }

    void destroy_all() noexcept {
        while (objects_ != nullptr) {
            GcObject* next = objects_->header.next_all;
            // Phase 5C: bump-allocated objects live in chunk memory — calling
            // `delete` would invoke ::operator delete on an interior pointer,
            // which is undefined behaviour.  Call the destructor explicitly;
            // the chunk pages themselves are released by ~NurserySpace() after
            // all other spaces (including OldSmallSpace) have been torn down.
            if ((objects_->header.flags & GcBumpAllocBit) != 0) {
                objects_->~GcObject();
            } else {
                delete objects_;
            }
            objects_ = next;
        }
        live_bytes_own_ = 0;
        // NOTE: do NOT call free_all_chunks() here. Promoted bump objects may
        // still be alive in old_small_.objects_ and reference chunk memory.
        // free_all_chunks() is called from ~NurserySpace() which runs after
        // Runtime::~Runtime() has already torn down all other spaces.
    }

    // C++ destructor: release chunk pages after all objects have been destroyed.
    // Runs AFTER Runtime::~Runtime() body (which calls destroy_all on all spaces),
    // so chunk memory is guaranteed to outlive any promoted bump objects in old_small_.
    ~NurserySpace() noexcept {
        free_all_chunks();
    }

    bool empty() const noexcept { return objects_ == nullptr; }

    // ── HeapSpace interface ───────────────────────────────────────────────────

    void for_each_object(const std::function<void(GcObject*)>& fn) override {
        for (GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all)
            fn(obj);
    }

    std::size_t live_bytes() const override { return live_bytes_own_; }

    void begin_cycle() override {
        sweep_cursor_     = nullptr;
        sweep_previous_   = nullptr;
        sweep_chain_head_ = nullptr;
    }

    // Called from Runtime::process_dirty_roots() when entering SweepObjects.
    // Detaches the existing nursery chain from new allocations (same pattern as
    // OldSmallSpace::begin_sweep — see that method's comment for full rationale).
    void begin_sweep() noexcept {
        sweep_cursor_     = objects_;
        sweep_previous_   = nullptr;
        sweep_chain_head_ = nullptr;
        objects_          = nullptr;  // isolate new allocs from sweep chain
    }

    // Full-GC sweep: delete white young objects; reset surviving Black→White.
    // Young-collection sweep is driven by Runtime::sweep_young_objects() which
    // accesses objects_/live_bytes_own_ directly; that path is non-incremental
    // and does not use sweep_cursor_ or sweep_chain_head_.
    void sweep(std::size_t& budget_work) override {
        while (sweep_cursor_ != nullptr && budget_work > 0) {
            GcObject* current = sweep_cursor_;
            GcObject* next    = current->header.next_all;
            if (current->header.color == GcColor::White) {
                if (sweep_previous_ != nullptr)
                    sweep_previous_->header.next_all = next;
                // else: current was sweep chain head with no prior survivor —
                // drop it without touching objects_ (new-alloc chain).
                live_bytes_own_ -= current->header.size_bytes;
                if (global_live_bytes_ != nullptr)
                    *global_live_bytes_ -= current->header.size_bytes;
                // Phase 5C: bump objects live in chunk memory — destructor only.
                // Dead bump slots stay in the chunk until reset_chunks() is called
                // after the next young collection.
                if ((current->header.flags & GcBumpAllocBit) != 0) {
                    current->~GcObject();
                } else {
                    delete current;
                }
            } else {
                current->header.color = GcColor::White;
                if (sweep_chain_head_ == nullptr) {
                    sweep_chain_head_ = current;  // first survivor
                }
                sweep_previous_ = current;
            }
            sweep_cursor_ = next;
            --budget_work;
        }
        // Same splice guard as OldSmallSpace::sweep() — see that method's comment.
        if (sweep_cursor_ == nullptr && sweep_chain_head_ != nullptr) {
            sweep_previous_->header.next_all = objects_;   // tail → new allocs
            objects_          = sweep_chain_head_;         // head of merged list
            sweep_chain_head_ = nullptr;
            sweep_previous_   = nullptr;
        }
    }

    [[nodiscard]] bool sweep_complete() const noexcept { return sweep_cursor_ == nullptr; }

    std::string verify() const override {
        std::size_t computed = 0;
        for (const GcObject* obj = objects_; obj != nullptr; obj = obj->header.next_all) {
            if (obj->header.space_kind == GcSpaceKind::Uninitialized) {
                return "NurserySpace::verify: Uninitialized space_kind (allocation path forgot to set it)";
            }
            if (obj->header.space_kind != GcSpaceKind::Nursery) {
                return "NurserySpace::verify: object has wrong space_kind";
            }
            if ((obj->header.flags & GcOldBit) != 0) {
                return "NurserySpace::verify: young object has GcOldBit set";
            }
            computed += obj->header.size_bytes;
        }
        if (computed != live_bytes_own_) {
            return "NurserySpace::verify: live_bytes_ accounting drift";
        }
        return {};
    }

    // ── Phase 5C: chunk management (public so Runtime can call reset_chunks) ──

    // Fast-path bump allocator: try to carve `sz` bytes (aligned to `align`)
    // from the current chunk.  Returns nullptr if the chunk is full or absent.
    [[nodiscard]] void* try_bump_alloc(std::size_t sz, std::size_t align) noexcept {
        if (current_chunk_ == nullptr) return nullptr;
        const auto raw   = reinterpret_cast<uintptr_t>(current_chunk_->bump_ptr);
        const auto aligned = (raw + align - 1) & ~(align - 1);
        if (aligned + sz > reinterpret_cast<uintptr_t>(current_chunk_->limit))
            return nullptr;
        current_chunk_->bump_ptr = reinterpret_cast<char*>(aligned + sz);
        return reinterpret_cast<void*>(aligned);
    }

    // Slow-path: acquire a recycled or freshly-allocated chunk, then retry.
    // Returns nullptr if `sz` will never fit in a single chunk (oversized).
    [[nodiscard]] void* alloc_bump_slow(std::size_t sz, std::size_t align) {
        // Oversized objects (can't fit even in an empty chunk) fall back to heap.
        if (sz + align > kNurseryChunkSize - kNurseryChunkDataOffset) return nullptr;

        NurseryChunkHeader* chunk;
        if (free_chunk_list_ != nullptr) {
            // Reuse a recycled chunk.
            chunk = free_chunk_list_;
            free_chunk_list_ = chunk->next_chunk;
            char* data_start = reinterpret_cast<char*>(chunk) + kNurseryChunkDataOffset;
            chunk->bump_ptr   = data_start;
            chunk->promoted_count = 0;
            // limit stays valid from original construction
        } else {
            // Allocate a fresh 256 KiB chunk from the OS heap.
            void* mem = ::operator new(kNurseryChunkSize);
            chunk = reinterpret_cast<NurseryChunkHeader*>(mem);
            char* data_start = reinterpret_cast<char*>(mem) + kNurseryChunkDataOffset;
            chunk->bump_ptr = data_start;
            chunk->limit    = reinterpret_cast<char*>(mem) + kNurseryChunkSize;
            chunk->promoted_count = 0;
        }
        // Push new chunk to front of active list.
        chunk->next_chunk = current_chunk_;
        current_chunk_    = chunk;

        return try_bump_alloc(sz, align);
    }

    // Find the chunk that contains the given bump-allocated object.
    // Searches active, retained, and free lists.  Returns nullptr if not found.
    NurseryChunkHeader* find_chunk(const void* ptr) const noexcept {
        auto search = [&](NurseryChunkHeader* head) -> NurseryChunkHeader* {
            for (auto* c = head; c != nullptr; c = c->next_chunk) {
                const auto base = reinterpret_cast<uintptr_t>(c);
                const auto addr = reinterpret_cast<uintptr_t>(ptr);
                if (addr >= base && addr < base + kNurseryChunkSize)
                    return c;
            }
            return nullptr;
        };
        if (auto* c = search(current_chunk_)) return c;
        if (auto* c = search(retained_chunks_)) return c;
        return nullptr;
    }

    // Increment the promoted counter for the chunk containing `obj`.
    void note_promoted(GcObject* obj) noexcept {
        auto* chunk = find_chunk(obj);
        if (chunk) ++chunk->promoted_count;
    }

    // Decrement the promoted counter when a promoted object dies (full GC sweep
    // or destroy_all).  If the chunk's counter reaches zero, move it to free list.
    void release_promoted_slot(GcObject* obj) noexcept {
        // Walk retained_chunks_ looking for the chunk that owns obj.
        NurseryChunkHeader** prev = &retained_chunks_;
        while (*prev != nullptr) {
            auto* c = *prev;
            const auto base = reinterpret_cast<uintptr_t>(c);
            const auto addr = reinterpret_cast<uintptr_t>(obj);
            if (addr >= base && addr < base + kNurseryChunkSize) {
                assert(c->promoted_count > 0);
                --c->promoted_count;
                if (c->promoted_count == 0) {
                    // Unlink from retained and move to free list.
                    *prev = c->next_chunk;
                    char* data_start = reinterpret_cast<char*>(c) + kNurseryChunkDataOffset;
                    c->bump_ptr   = data_start;
                    c->next_chunk = free_chunk_list_;
                    free_chunk_list_ = c;
                }
                return;
            }
            prev = &(*prev)->next_chunk;
        }
    }

    // Called after young collection when all survivors have been promoted and
    // nursery_.objects_ is nullptr.  Chunks with promoted_count > 0 are moved
    // to retained_chunks_ (kept alive for promoted objects).  Chunks with
    // promoted_count == 0 are moved to free_chunk_list_ for reuse.
    void reset_chunks() noexcept {
        assert(objects_ == nullptr
            && "reset_chunks: nursery objects_ must be empty before chunk reset; "
               "ensure all survivors were promoted or freed first");
        NurseryChunkHeader* c = current_chunk_;
        while (c != nullptr) {
            NurseryChunkHeader* nxt = c->next_chunk;
            if (c->promoted_count > 0) {
                // Chunk has promoted objects — retain it.
                c->next_chunk = retained_chunks_;
                retained_chunks_ = c;
            } else {
                // No promoted objects — recycle for reuse.
                char* data_start = reinterpret_cast<char*>(c) + kNurseryChunkDataOffset;
                c->bump_ptr   = data_start;
                c->next_chunk = free_chunk_list_;
                free_chunk_list_ = c;
            }
            c = nxt;
        }
        current_chunk_ = nullptr;
    }

private:
    void remove_from_list(GcObject* object) noexcept {
        GcObject** prev = &objects_;
        while (*prev != nullptr) {
            if (*prev == object) {
                *prev = object->header.next_all;
                object->header.next_all = nullptr;
                return;
            }
            prev = &(*prev)->header.next_all;
        }
    }

    // Release all chunks (both active and free lists) back to the OS heap.
    // Called from destroy_all() on VM shutdown.
    void free_all_chunks() noexcept {
        auto free_list = [](NurseryChunkHeader* head) noexcept {
            while (head != nullptr) {
                NurseryChunkHeader* nxt = head->next_chunk;
                ::operator delete(head);
                head = nxt;
            }
        };
        free_list(current_chunk_);
        free_list(free_chunk_list_);
        free_list(retained_chunks_);
        current_chunk_   = nullptr;
        free_chunk_list_ = nullptr;
        retained_chunks_ = nullptr;
    }

    std::size_t* global_live_bytes_ = nullptr;
};

// ──────────────────────────────────────────────────────────────────────────────

struct Guid128Hash {
    std::size_t operator()(const ZephyrGuid128& value) const noexcept {
        return std::hash<std::uint64_t>{}(value.high) ^ (std::hash<std::uint64_t>{}(value.low) << 1U);
    }
};

enum class GcCollectionKind : std::uint8_t {
    Full,
    Young,
};

struct HostHandleResolution {
    HostHandleEntry* entry = nullptr;
    void* instance = nullptr;
    std::shared_ptr<void> keep_alive;
};

struct RetainedCallbackRecord {
    ZephyrFunctionHandle function;
};

// ── Phase 4C: compile-time routing trait ────────────────────────────────────
// Marks GcObject subclasses that should be routed to OldSmallSpace at birth
// (mirrors the runtime should_allocate_old() check but evaluated at compile
// time so that allocate<T>() can reserve a slab slot BEFORE constructing T,
// avoiding any double-construction of forwarded arguments).
template <typename T> struct GcRoutesToOldSmall : std::false_type {};
template <> struct GcRoutesToOldSmall<Environment>    : std::true_type {};
template <> struct GcRoutesToOldSmall<CoroutineObject> : std::true_type {};

class Runtime {
public:
    explicit Runtime(ZephyrVMConfig config = {});
    ~Runtime();

    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        // ── Phase 4C slab fast-path ───────────────────────────────────────────
        // GcRoutesToOldSmall<T> is a compile-time trait (true for Environment
        // and CoroutineObject). Both checks are constexpr so the entire block
        // compiles away for types that don't route to old-small, and we never
        // call new T() before knowing we have a slot — avoiding double-
        // construction of potentially-moved forwarding arguments.
        if constexpr (GcRoutesToOldSmall<T>::value &&
                      OldSmallSpace::slot_size_for(sizeof(T)) != 0) {
            void* slab_slot = old_small_.try_alloc_slab(sizeof(T));
            if (slab_slot != nullptr) {
                constexpr std::size_t slot_sz = OldSmallSpace::slot_size_for(sizeof(T));
                T* object = new (slab_slot) T(std::forward<Args>(args)...);
                object->header.size_bytes = static_cast<std::uint32_t>(slot_sz);
                object->header.color      = gc_marking() ? GcColor::Black : GcColor::White;
                object->header.flags     &= static_cast<std::uint8_t>(
                    ~(GcDirtyQueuedBit | GcMinorRememberedBit));
                object->header.flags     |= GcOldBit | GcSlabBit;
                object->header.age        = 0;
                object->header.space_kind = GcSpaceKind::OldSmall;
                object->header.next_gray  = nullptr;
                live_bytes_              += slot_sz;
                old_small_.insert(object);
                allocation_pressure_bytes_ += slot_sz;
                ++total_allocations_;
                if (gc_phase_ == ZephyrGcPhase::Idle &&
                    allocation_pressure_bytes_ >= incremental_trigger_bytes_) {
                    gc_cycle_requested_ = true;
                }
                return object;
            }
            // Slab full (or no class for sizeof(T)): fall through to new T().
        }

        // ── Phase 5C nursery bump fast-path ──────────────────────────────────
        // Route non-old-gen, non-oversized types through the bump allocator.
        // GcRoutesToOldSmall<T> compile-time gate ensures we skip this for
        // Environment / CoroutineObject (which always go to OldSmallSpace).
        // The size guard skips the bump path for objects that would be routed
        // to LargeObjectSpace at runtime (large_object_threshold_bytes_ is
        // typically 8 KiB; chunks are 256 KiB so the bump path is safe for
        // anything below the threshold that doesn't qualify as old).
        if constexpr (!GcRoutesToOldSmall<T>::value) {
            if (sizeof(T) < large_object_threshold_bytes_) {
                void* slot = nursery_.try_bump_alloc(sizeof(T), alignof(T));
                if (slot == nullptr)
                    slot = nursery_.alloc_bump_slow(sizeof(T), alignof(T));
                if (slot != nullptr) {
                    T* object = new (slot) T(std::forward<Args>(args)...);
                    object->header.size_bytes = static_cast<std::uint32_t>(sizeof(T));
                    object->header.color      = gc_marking() ? GcColor::Black : GcColor::White;
                    object->header.flags     &= static_cast<std::uint8_t>(
                        ~(GcDirtyQueuedBit | GcMinorRememberedBit | GcOldBit));
                    object->header.flags     |= GcBumpAllocBit;
                    object->header.age        = 0;
                    object->header.space_kind = GcSpaceKind::Nursery;
                    object->header.next_gray  = nullptr;
                    live_bytes_              += sizeof(T);
                    nursery_.insert(object);  // links into objects_, bumps live_bytes_own_
                    allocation_pressure_bytes_       += sizeof(T);
                    young_allocation_pressure_bytes_ += sizeof(T);
                    ++total_allocations_;
                    if (gc_phase_ == ZephyrGcPhase::Idle &&
                        young_allocation_pressure_bytes_ >= nursery_trigger_bytes_)
                        gc_young_cycle_requested_ = true;
                    if (gc_phase_ == ZephyrGcPhase::Idle &&
                        allocation_pressure_bytes_ >= incremental_trigger_bytes_)
                        gc_cycle_requested_ = true;
                    return object;
                }
                // Chunk full and oversized for any chunk — fall through to new T().
            }
        }

        // ── Normal allocation path ────────────────────────────────────────────
        auto* object = new T(std::forward<Args>(args)...);
        object->header.size_bytes = static_cast<std::uint32_t>(sizeof(T));
        object->header.color = gc_marking() ? GcColor::Black : GcColor::White;
        object->header.flags &= static_cast<std::uint8_t>(~(GcDirtyQueuedBit | GcMinorRememberedBit));
        object->header.age = 0;
        const auto object_kind = static_cast<GcObject*>(object)->kind;
        live_bytes_ += object->header.size_bytes;
        if (object->header.size_bytes >= large_object_threshold_bytes_) {
            // Large-object path: tracked by LargeObjectSpace, always old-gen.
            object->header.flags |= GcOldBit;
            object->header.space_kind = GcSpaceKind::LargeObject;
            object->header.next_gray = nullptr;
            los_.insert(object);
            // los_.live_bytes_ updated inside insert(); global live_bytes_ updated above.
        } else if (should_allocate_old(object_kind, object->header.size_bytes)) {
            object->header.flags |= GcOldBit;
            object->header.space_kind = GcSpaceKind::OldSmall;
            object->header.next_gray = nullptr;
            old_small_.insert(object);
        } else {
            object->header.flags &= static_cast<std::uint8_t>(~GcOldBit);
            object->header.space_kind = GcSpaceKind::Nursery;
            object->header.next_gray = nullptr;
            nursery_.insert(object);
        }
        allocation_pressure_bytes_ += object->header.size_bytes;
        if ((object->header.flags & GcOldBit) == 0) {
            young_allocation_pressure_bytes_ += object->header.size_bytes;
        }
        ++total_allocations_;
        if (gc_phase_ == ZephyrGcPhase::Idle && young_allocation_pressure_bytes_ >= nursery_trigger_bytes_) {
            gc_young_cycle_requested_ = true;
        }
        if (gc_phase_ == ZephyrGcPhase::Idle && allocation_pressure_bytes_ >= incremental_trigger_bytes_) {
            gc_cycle_requested_ = true;
        }
        return object;
    }

    // Allocate an object directly into PinnedSpace (Phase 3B+).
    // Pinned objects are permanent roots that survive every GC cycle.
    // They are never freed by the sweep — only by explicit release (e.g., module unload).
    template <typename T, typename... Args>
    T* allocate_pinned(Args&&... args) {
        auto* object = new T(std::forward<Args>(args)...);
        object->header.size_bytes = static_cast<std::uint32_t>(sizeof(T));
        object->header.color      = gc_marking() ? GcColor::Black : GcColor::White;
        object->header.flags     &= static_cast<std::uint8_t>(~(GcDirtyQueuedBit | GcMinorRememberedBit));
        object->header.flags     |= GcPinnedBit | GcOldBit;
        object->header.age        = 0;
        object->header.space_kind = GcSpaceKind::Pinned;
        object->header.next_gray  = nullptr;
        pinned_.insert(object);        // pinned_.live_bytes_ updated inside insert()
        live_bytes_ += object->header.size_bytes;
        allocation_pressure_bytes_ += object->header.size_bytes;
        // Pinned objects are old-gen; do not update young_allocation_pressure_bytes_.
        ++total_allocations_;
        if (gc_phase_ == ZephyrGcPhase::Idle && allocation_pressure_bytes_ >= incremental_trigger_bytes_) {
            gc_cycle_requested_ = true;
        }
        return object;
    }

    void gc_step(std::size_t budget_work);
    void set_gc_stress(bool enabled, std::size_t budget_work);
    void advance_frame(std::size_t gc_budget_work);
    void advance_tick(std::size_t gc_budget_work);
    void advance_scene();
    void collect_young();
    void collect_garbage();
    VoidResult gc_verify_young();
    VoidResult gc_verify_full();

    // Phase 7 (compaction): evacuate promoted bump objects from nursery chunks
    // into OldSmallSpace slab slots.  Must be called at Idle phase (typically
    // after a full collection).  Frees nursery chunks whose promoted_count
    // drops to zero, reducing long-term memory usage.
    void compact_old_generation();
    void invalidate_host_handle(const ZephyrHostObjectRef& handle);
    ZephyrVM::RuntimeStats runtime_stats() const;
    std::uint64_t gc_pause_percentile(int pct) const;
    GCPauseStats get_gc_pause_stats() const;
    void start_profiling();
    ZephyrProfileReport stop_profiling();
    void start_gc_trace();
    void stop_gc_trace();
    bool is_gc_trace_active() const;
    std::string get_gc_trace_json() const;
    void start_coroutine_trace();
    void stop_coroutine_trace();
    void enable_bytecode_cache(bool enabled = true);
    void clear_bytecode_cache();
    std::size_t bytecode_cache_size() const;
    void add_module_search_path(const std::string& path);
    std::vector<std::string> get_module_search_paths() const;
    void clear_module_search_paths();
    void set_package_root(const std::string& path);
    std::string debug_dump_coroutines() const;
    std::string dump_bytecode(const std::string& module_name, const std::string& function_name) const;
    void set_breakpoint(const ZephyrBreakpoint& bp);
    void clear_breakpoints();
    ZephyrSnapshot snapshot_state() const;
    bool restore_snapshot_state(const ZephyrSnapshot& snapshot);

    RuntimeResult<Value> execute_string_module(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir);
    RuntimeResult<Value> execute_file_module(const std::filesystem::path& path);
    VoidResult check_source(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir);
    VoidResult check_file(const std::filesystem::path& path);

    std::optional<ZephyrFunctionHandle> find_function(const std::string& module_name, const std::string& function_name);
    ZephyrScriptCallbackHandle capture_callback(const ZephyrFunctionHandle& handle);
    void release_callback(const ZephyrScriptCallbackHandle& handle);
    RuntimeResult<Value> call_callback_handle(const ZephyrScriptCallbackHandle& handle, const std::vector<ZephyrValue>& args);
    RuntimeResult<ZephyrCoroutineHandle> spawn_coroutine_handle(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args);
    RuntimeResult<Value> resume_coroutine_handle(const ZephyrCoroutineHandle& handle);
    void cancel_coroutine_handle(const ZephyrCoroutineHandle& handle);
    std::optional<ZephyrCoroutineInfo> query_coroutine_handle(const ZephyrCoroutineHandle& handle) const;
    RuntimeResult<Value> call_handle(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args);
    RuntimeResult<ZephyrValue> serialize_public_value(const ZephyrValue& value);
    RuntimeResult<ZephyrValue> deserialize_public_value(const ZephyrValue& value);
    RuntimeResult<ZephyrValue> serialize_runtime_value(const Value& value, const Span& span, const std::string& module_name);
    RuntimeResult<ZephyrValue> serialize_runtime_node(const Value& value, const Span& span, const std::string& module_name);
    RuntimeResult<Value> deserialize_public_payload(const ZephyrValue& value, const Span& span, const std::string& module_name);
    RuntimeResult<Value> deserialize_serialized_node(const ZephyrValue& value, const Span& span, const std::string& module_name);

    Value from_public_value(const ZephyrValue& value);
    ZephyrValue to_public_value(const Value& value);

    void register_module(const std::string& module_name, const std::function<void(ZephyrModuleBinder&)>& initializer) {
        host_modules_[module_name] = HostModuleRecord{initializer};
    }

    void register_global_function(const std::string& name, ZephyrNativeFunction function, std::vector<std::string> param_types,
                                  std::string return_type);
    void install_core();

    ZephyrValue make_host_object(std::shared_ptr<ZephyrHostClass> host_class, std::shared_ptr<void> instance) {
        return ZephyrValue(ZephyrHostObjectRef{std::move(host_class), std::move(instance)});
    }

    void define_value(Environment* environment, const std::string& name, Value value, bool mutable_value,
                      std::optional<std::string> type_name = std::nullopt);

private:
    struct PublicArgsBufferLease {
        Runtime* runtime = nullptr;
        std::size_t index = 0;

        PublicArgsBufferLease() = default;
        PublicArgsBufferLease(Runtime& owner, std::size_t buffer_index) : runtime(&owner), index(buffer_index) {}
        PublicArgsBufferLease(const PublicArgsBufferLease&) = delete;
        PublicArgsBufferLease& operator=(const PublicArgsBufferLease&) = delete;
        PublicArgsBufferLease(PublicArgsBufferLease&& other) noexcept : runtime(other.runtime), index(other.index) {
            other.runtime = nullptr;
        }
        PublicArgsBufferLease& operator=(PublicArgsBufferLease&& other) noexcept {
            if (this != &other) {
                release();
                runtime = other.runtime;
                index = other.index;
                other.runtime = nullptr;
            }
            return *this;
        }
        ~PublicArgsBufferLease() { release(); }

        std::vector<ZephyrValue>& args() const;

    private:
        void release();
    };

    struct FlowSignal {
        enum class Kind : std::uint8_t {
            None,
            Return,
            Break,
            Continue,
            ErrorPropagate,
        };

        Kind kind = Kind::None;
        std::string label;
        Value value = Value::nil();
        bool is_propagated_error = false; // true when error should propagate up to caller
    };

    using FlowResult = RuntimeResult<FlowSignal>;
    struct CoroutineExecutionResult {
        bool yielded = false;
        Value value = Value::nil();
        std::size_t step_count = 0;
    };
    struct TraitDefinition {
        std::string name;
        std::unordered_map<std::string, TraitMethodDecl> methods;
    };
    struct ImplMethodEntry {
        std::string module_name;
        std::string binding_name;
    };
    struct ImplDefinition {
        std::string trait_name;
        std::string type_name;
        std::unordered_map<std::string, ImplMethodEntry> methods;
    };
    struct CheckFunctionInfo {
        std::string module_name;
        std::string name;
        Span span;
        std::vector<Param> params;
        std::optional<TypeRef> return_type;
    };
    struct CheckTraitInfo {
        std::string module_name;
        std::string name;
        Span span;
        std::unordered_map<std::string, TraitMethodDecl> methods;
    };
    struct CheckModuleSummary {
        std::string module_name;
        std::unordered_set<std::string> exports;
        std::unordered_map<std::string, CheckFunctionInfo> functions;
        std::unordered_map<std::string, CheckTraitInfo> traits;
        std::unordered_map<std::string, std::string> import_aliases;
        std::vector<std::string> imported_modules;
    };

    std::shared_ptr<BytecodeFunction> compile_bytecode_function(const std::string& name, const std::vector<Param>& params, BlockStmt* body, const std::vector<std::string>& generic_params = {});
    std::shared_ptr<BytecodeFunction> compile_module_bytecode(const std::string& name, Program* program);
    RuntimeResult<Value> execute_bytecode_chunk(const BytecodeFunction& chunk, const std::vector<Param>& params, Environment* call_env,
                                                ModuleRecord& module, const Span& call_span,
                                                const std::vector<UpvalueCellObject*>* captured_upvalues = nullptr,
                                                const std::vector<Value>* lightweight_args = nullptr);
    RuntimeResult<Value> execute_register_bytecode(const BytecodeFunction& chunk, const std::vector<Param>& params, Environment* call_env,
                                                   ModuleRecord& module, const Span& call_span,
                                                   const std::vector<UpvalueCellObject*>* captured_upvalues,
                                                   const std::vector<Value>* call_args);
    RuntimeResult<CoroutineExecutionResult> resume_coroutine_single_frame(CoroutineObject* coroutine, ModuleRecord& module, const Span& call_span);
    RuntimeResult<CoroutineExecutionResult> resume_coroutine_bytecode(CoroutineObject* coroutine, ModuleRecord& module, const Span& call_span);
    RuntimeResult<CoroutineExecutionResult> resume_nested_coroutine_frame(CoroutineObject* coroutine, ModuleRecord& module, const Span& call_span);
    void refresh_coroutine_locals_from_env(CoroutineFrameState& frame);
    void register_suspended_coroutine(CoroutineObject* coroutine);
    void unregister_suspended_coroutine(CoroutineObject* coroutine);
    void compact_suspended_coroutine(CoroutineObject* coroutine);
    VoidResult push_coroutine_script_frame(CoroutineObject* coroutine, ScriptFunctionObject* function, const std::vector<Value>& args,
                                           const Span& call_span, const std::string& module_name);
    RuntimeResult<Value> execute_bytecode(ScriptFunctionObject* function, Environment* call_env, ModuleRecord& module, const Span& call_span);
    RuntimeResult<Value> run_program(ModuleRecord& module);
    RuntimeResult<Value> call_value(const Value& callee, const std::vector<Value>& args, const Span& call_span, const std::string& module_name);
    RuntimeResult<Value> evaluate(Environment* environment, Expr* expr, const std::string& module_name);
    FlowResult execute(Environment* environment, Stmt* stmt, ModuleRecord& module);
    FlowResult execute_block(Environment* environment, BlockStmt* block, ModuleRecord& module);

    RuntimeResult<Value> evaluate_literal(const LiteralExpr& expr);
    RuntimeResult<Value> evaluate_interpolated_string(Environment* environment, InterpolatedStringExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_array(Environment* environment, ArrayExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_binary(Environment* environment, BinaryExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_unary(Environment* environment, UnaryExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_assign(Environment* environment, AssignExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_member(Environment* environment, MemberExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_optional_member(Environment* environment, OptionalMemberExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_index(Environment* environment, IndexExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_call(Environment* environment, CallExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_optional_call(Environment* environment, OptionalCallExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_function(Environment* environment, FunctionExpr& expr);
    RuntimeResult<Value> evaluate_coroutine(Environment* environment, CoroutineExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_resume(Environment* environment, ResumeExpr& expr, const std::string& module_name);
    RuntimeResult<Value> resume_coroutine_value(const Value& value, const Span& span, const std::string& module_name);
    RuntimeResult<Value> evaluate_struct_init(Environment* environment, StructInitExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_enum_init(Environment* environment, EnumInitExpr& expr, const std::string& module_name);
    RuntimeResult<Value> evaluate_match(Environment* environment, MatchExpr& expr, const std::string& module_name);
    RuntimeResult<Value> apply_binary_op(TokenType op, const Value& left, const Value& right, const Span& span, const std::string& module_name);
    RuntimeResult<Value> apply_unary_op(TokenType op, const Value& right, const Span& span, const std::string& module_name);
    TypeRef parse_type_name(const std::string& text, const Span& span) const;
    RuntimeResult<Value> load_bytecode_constant(const BytecodeFunction& function, int index);
    RuntimeResult<Value> build_struct_value(Environment* environment, const std::string& type_name, const std::vector<std::string>& field_names,
                                           const std::vector<Value>& field_values, const Span& span, const std::string& module_name);
    RuntimeResult<Value> build_enum_value(Environment* environment, const std::string& enum_name, const std::string& variant_name,
                                         const std::vector<Value>& payload, const Span& span, const std::string& module_name);
    RuntimeResult<Value> get_enum_payload_value(const Value& value, int index, const Span& span, const std::string& module_name);
    RuntimeResult<bool> is_enum_variant_value(const Value& value, const std::string& enum_name, const std::string& variant_name,
                                              int payload_count, const Span& span, const std::string& module_name);
    RuntimeResult<Value> load_member_value(const Value& object,
                                           const CompactInstruction& instruction,
                                           const InstructionMetadata& metadata,
                                           const std::string& module_name);
    RuntimeResult<Value> store_member_value(const Value& object,
                                            const Value& value,
                                            const CompactInstruction& instruction,
                                            const InstructionMetadata& metadata,
                                            const std::string& module_name);
    RuntimeResult<Value> get_member_value(const Value& object, const std::string& member, const Span& span, const std::string& module_name);
    RuntimeResult<Value> set_member_value(const Value& object, const std::string& member, const Value& value, const Span& span, const std::string& module_name);
    RuntimeResult<Value> get_index_value(const Value& object, const Value& index, const Span& span, const std::string& module_name);
    RuntimeResult<Value> set_index_value(const Value& object, const Value& index, const Value& value, const Span& span, const std::string& module_name);
    RuntimeResult<Value> call_member_value(const Value& object, const std::string& member, const std::vector<Value>& args, const Span& span,
                                           const std::string& module_name);
    VoidResult register_trait_decl(Environment* environment, TraitDecl* trait_decl, ModuleRecord& module);
    VoidResult register_impl_decl(Environment* environment, ImplDecl* impl_decl, ModuleRecord& module);
    RuntimeResult<std::optional<Value>> resolve_trait_method(const Value& receiver,
                                                             const std::string& member,
                                                             const Span& span,
                                                             const std::string& module_name);
    void check_breakpoint(std::size_t ip, std::uint32_t current_line, const std::string& module_name);
    Value make_string(std::string value);
    Value make_literal_string(std::string value);
    StringObject* intern_string(const std::string& value);
    HostHandleToken register_host_handle(const ZephyrHostObjectRef& host_object);
    RuntimeResult<HostHandleResolution> resolve_host_handle(const Value& value, const Span& span, const std::string& module_name,
                                                            std::string_view action);
    RuntimeResult<HostHandleEntry*> lookup_host_handle_entry(HostHandleToken token, const Span& span, const std::string& module_name,
                                                             std::string_view action);
    PublicArgsBufferLease acquire_public_args_buffer(std::size_t required_capacity);
    VoidResult validate_handle_store(const Value& value, HandleContainerKind container, const Span& span, const std::string& module_name,
                                     const std::string& context);
    UpvalueCellObject* ensure_binding_cell(Environment* owner, const std::string& binding_name, Binding& binding, HandleContainerKind container);
    RuntimeResult<std::vector<UpvalueCellObject*>> capture_upvalue_cells(Environment* environment,
                                                                         const std::vector<std::string>& upvalue_names,
                                                                         HandleContainerKind container,
                                                                         const Span& span,
                                                                         const std::string& module_name);
    RuntimeResult<ScriptFunctionObject*> create_script_function(const std::string& name,
                                                                const std::string& module_name,
                                                                const std::vector<Param>& params,
                                                                const std::optional<TypeRef>& return_type,
                                                                BlockStmt* body,
                                                                Environment* closure,
                                                                std::shared_ptr<BytecodeFunction> bytecode,
                                                                const Span& span,
                                                                const std::vector<std::string>& generic_params = {});
    VoidResult ensure_capture_cells(Environment* environment, HandleContainerKind container, const Span& span, const std::string& module_name);
    VoidResult validate_closure_capture(Environment* environment, const Span& span, const std::string& module_name);
    Environment* module_or_root_environment(Environment* environment) const;
    Environment* select_closure_environment(Environment* closure, const std::shared_ptr<BytecodeFunction>& bytecode) const;
    VoidResult ensure_ast_fallback_bytecode_supported(const BytecodeFunction* bytecode, const Span& span, const std::string& module_name,
                                                      const std::string& context) const;
    void install_upvalue_bindings(Environment* environment, const BytecodeFunction& bytecode,
                                  const std::vector<UpvalueCellObject*>& captured_upvalues);
    [[noreturn]] void trap_invalid_handle(const std::string& message) const;
    ZephyrInvalidAccessMode effective_invalid_access_mode(const HostHandleEntry& entry) const;
    void invalidate_host_handle_slot(std::uint32_t slot, const std::string& reason);
    RuntimeResult<std::uint32_t> find_host_handle_slot(const ZephyrHostObjectRef& host_object) const;
    ZephyrHostHandlePolicy default_policy_for_kind(ZephyrHostHandleKind kind) const;
    bool handle_store_allowed(ZephyrHostHandleLifetime lifetime, const ZephyrHostHandlePolicy& policy, HandleContainerKind container) const;
    bool handle_can_cross_scene(ZephyrHostHandleLifetime lifetime, const ZephyrHostHandlePolicy& policy) const;
    void begin_profile_scope(const std::string& function_name);
    void end_profile_scope();
    std::uint64_t current_runtime_timestamp_ns() const;
    std::uint64_t module_file_mtime(const std::filesystem::path& path) const;
    void record_gc_trace_event(GCTraceEvent::Type type, std::size_t heap_bytes_before, std::size_t heap_bytes_after);
    void ensure_coroutine_trace_id(CoroutineObject* coroutine);
    void record_coroutine_trace_event(CoroutineTraceEvent::Type type, const CoroutineObject* coroutine);
    void record_coroutine_completed(CoroutineObject* coroutine);
    void record_coroutine_destroyed(CoroutineObject* coroutine);

    Binding* lookup_binding(Environment* environment, const std::string& name);
    RuntimeResult<Value> lookup_value(Environment* environment, const std::string& name, const Span& span, const std::string& module_name);
    VoidResult assign_value(Environment* environment, const std::string& name, Value value, const Span& span, const std::string& module_name);
    bool is_truthy(const Value& value) const;
    bool values_equal(const Value& left, const Value& right) const;
    bool matches_type(const Value& value, const std::string& type_name) const;
    std::string describe_value_type(const Value& value) const;
    std::string append_stack_frame(std::string message, const std::string& function_name, const std::string& module_name, const Span& span) const;
    std::string append_coroutine_stack_trace(std::string message, const CoroutineObject* coroutine) const;
    std::string describe_match_missing_case(const Value& value) const;
    VoidResult enforce_type(const Value& value, const std::optional<std::string>& type_name, const Span& span,
                            const std::string& module_name, const std::string& context) const;
    std::string value_to_string(const Value& value) const;
    void emit_warning(const std::string& module_name, const Span& span, const std::string& message) const;

    RuntimeResult<StructTypeObject*> expect_struct_type(Environment* environment, const TypeRef& type_name, const std::string& module_name, const Span& span);
    RuntimeResult<EnumTypeObject*> expect_enum_type(Environment* environment, const TypeRef& type_name, const std::string& module_name, const Span& span);
    RuntimeResult<ModuleNamespaceObject*> expect_module_namespace(const Value& value, const Span& span, const std::string& module_name);

    RuntimeResult<bool> bind_pattern(Environment* target_env, const Value& value, Pattern* pattern, const std::string& module_name);
    std::filesystem::path resolve_import_path(const std::filesystem::path& base_dir, const std::string& path) const;
    RuntimeResult<ModuleRecord*> import_module(const std::filesystem::path& base_dir, const std::string& specifier);
    VoidResult import_exports(Environment* environment, ModuleRecord& imported, std::optional<std::string> alias,
                              const std::string& module_name, const Span& span);
    RuntimeResult<std::unique_ptr<Program>> parse_source(const std::string& source, const std::string& module_name);
    VoidResult check_program_recursive(const std::string& module_name, const std::filesystem::path& base_dir, Program& program,
                                       std::unordered_set<std::string>& visiting, std::unordered_set<std::string>& checked,
                                       std::unordered_map<std::string, CheckModuleSummary>& summaries);
    RuntimeResult<ModuleRecord*> load_file_record(const std::filesystem::path& path);
    RuntimeResult<ModuleRecord*> load_virtual_record(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir);
    RuntimeResult<ModuleRecord*> load_host_module(const std::string& module_name);
    std::string canonical_module_key(const std::filesystem::path& path) const;
    bool should_allocate_old(ObjectKind kind, std::size_t size_bytes) const;
    bool is_old_object(const GcObject* object) const;
    bool is_large_object(const GcObject* object) const;
    void promote_object(GcObject* object);
    void record_gc_pause(std::uint64_t duration_ns, bool is_full);
    void perform_young_collection();
    void sweep_young_objects();

    // Phase 7 (compaction) helpers
    static bool is_object_relocatable(const GcObject* obj) noexcept;
    GcObject* relocate_object(GcObject* old_obj, void* dest);
    void fixup_object_references(GcObject* object);
    void fixup_root_references();
    std::string describe_bytecode_constant(const BytecodeConstant& constant) const;
public:
    void mark_value(const Value& value);
    void mark_object(GcObject* object);
    void mark_young_value(const Value& value);
    void mark_young_object(GcObject* object);
private:
    void mark_young_root_value(const Value& value);
    void visit_root_references(const std::function<void(GcObject*)>& object_visitor,
                               const std::function<void(const Value&)>& value_visitor) const;
    void visit_object_references(const GcObject* object, const std::function<void(GcObject*)>& object_visitor,
                                 const std::function<void(const Value&)>& value_visitor) const;
    void trace_young_references(const GcObject* object);
    void trace_young_environment(Environment* environment);
    void trace_young_array(ArrayObject* array);
    void trace_young_struct(StructInstanceObject* instance);
    void trace_young_enum(EnumInstanceObject* instance);
    void trace_young_coroutine(CoroutineObject* coroutine);
    void trace_young_value_cards(const std::vector<Value>& values, std::vector<std::uint64_t>& cards);
    bool value_card_has_young_reference(const std::vector<Value>& values, std::size_t card_index) const;
    bool environment_card_has_young_reference(const Environment* environment, std::size_t card_index) const;
    bool struct_card_has_young_reference(const StructInstanceObject* instance, std::size_t card_index) const;
    bool owner_is_fully_card_tracked(const GcObject* object) const;
    bool owner_has_dirty_minor_cards(const GcObject* object) const;
    bool has_direct_young_reference(const GcObject* object) const;
    void remember_minor_owner(GcObject* object);
    void compact_minor_remembered_set();
    void rebuild_minor_remembered_set();
    void rebuild_environment_cards(Environment* environment);
    void rebuild_array_cards(ArrayObject* array);
    void rebuild_struct_cards(StructInstanceObject* instance);
    void rebuild_enum_cards(EnumInstanceObject* instance);
    void rebuild_coroutine_cards(CoroutineObject* coroutine);
    void note_environment_binding_write(Environment* environment, std::size_t binding_index, const Value& value);
    void note_array_element_write(ArrayObject* array, std::size_t index, const Value& value);
    void note_struct_field_write(StructInstanceObject* instance, std::size_t field_index, const Value& value);
    void note_enum_payload_write(EnumInstanceObject* instance, std::size_t index, const Value& value);
    std::size_t count_remembered_cards() const;
    void mark_roots();
    void request_gc_cycle();
    void begin_gc_cycle();
    void seed_roots();
    void process_dirty_roots();
    void drain_gray(std::size_t& budget_work);
    void sweep(std::size_t& budget_work);
    void process_detach_queue(std::size_t& budget_work);
    void maybe_run_gc_stress_safe_point();
    void note_write(Environment* environment, const Value& value);
    void note_write(GcObject* owner, const Value& value);
    bool gc_marking() const;
    bool accounting_consistent() const;  // Phase 0 helper; body grows per phase.

    // ── Phase 6: four-space heap (LegacyHeapSpace removed; was empty after Phase 5B)
    LargeObjectSpace los_;       // Phase 2A/2B: >= large_object_threshold_bytes_
    PinnedSpace      pinned_;    // Phase 3A/3B: permanent roots (root_env, modules)
    OldSmallSpace    old_small_; // Phase 4A/4B: old non-large (slab mode in 4C)
    NurserySpace     nursery_;   // Phase 5A/5B: young-gen (bump-ptr in 5C)
    std::vector<HeapSpace*> all_spaces_;  // = {&los_, &pinned_, &old_small_, &nursery_}
    // ──────────────────────────────────────────────────────────────────────
    std::vector<GcObject*> gray_stack_;
    Environment* root_environment_ = nullptr;
    std::vector<Environment*> active_environments_;
    std::vector<const std::vector<Value>*> rooted_value_vectors_;
    std::vector<const Value*> rooted_values_;
    std::vector<Environment*> dirty_root_environments_;
    std::vector<GcObject*> dirty_objects_;
    std::vector<GcObject*> remembered_objects_;
    std::vector<GcObject*> detach_queue_;
    std::unordered_map<std::string, StringObject*> interned_strings_;
    std::vector<std::filesystem::path> module_search_paths_;
    std::vector<NativeFunctionObject*> native_callback_registry_;
    std::vector<GcObject*> pinned_debug_objects_;
    std::vector<CoroutineObject*> active_coroutines_;
    std::unordered_set<CoroutineObject*> suspended_coroutines_;
    std::vector<std::vector<ZephyrValue>> public_args_buffers_;
    std::vector<std::size_t> free_public_args_buffer_indices_;
    std::unordered_map<std::string, HostModuleRecord> host_modules_;
    std::unordered_map<std::string, ModuleRecord> modules_;
    std::unordered_map<std::string, TraitDefinition> traits_;
    std::unordered_map<std::string, std::unordered_map<std::string, ImplDefinition>> trait_impls_;
    std::vector<HostHandleEntry> host_handles_;
    std::unordered_map<ZephyrGuid128, std::uint32_t, Guid128Hash> stable_handle_lookup_;
    std::unordered_map<std::uint64_t, RetainedCallbackRecord> retained_callbacks_;
    std::unordered_map<std::uint64_t, CoroutineObject*> retained_coroutines_;
    ZephyrVMConfig config_{};
    ZephyrGcPhase gc_phase_ = ZephyrGcPhase::Idle;
    GcCollectionKind gc_collection_kind_ = GcCollectionKind::Full;
    bool gc_cycle_requested_ = false;
    bool gc_young_cycle_requested_ = false;
    bool profiling_active_ = false;
    std::size_t live_bytes_ = 0;
    std::size_t total_allocations_ = 0;
    std::size_t total_gc_steps_ = 0;
    std::size_t total_gc_cycles_ = 0;
    std::size_t total_minor_gc_cycles_ = 0;
    std::size_t total_major_gc_cycles_ = 0;
    std::size_t total_promotions_ = 0;
    std::size_t total_compactions_ = 0;  // Phase 7: objects relocated by compact_old_generation
    std::size_t total_young_collections_ = 0;
    std::size_t total_full_collections_ = 0;
    std::size_t total_gc_verifications_ = 0;
    std::size_t total_gc_stress_safe_points_ = 0;
    std::size_t total_remembered_card_fast_prunes_ = 0;
    std::size_t barrier_hits_ = 0;
    std::size_t invalid_handle_faults_ = 0;
    std::size_t handle_resolve_count_ = 0;
    std::size_t handle_resolve_failures_ = 0;
    std::size_t stable_resolve_hits_ = 0;
    std::size_t stable_resolve_misses_ = 0;
    bool dap_active_ = false;
    std::size_t coroutine_compactions_ = 0;
    std::size_t coroutine_compacted_frames_ = 0;
    std::size_t coroutine_compacted_capacity_ = 0;
    std::size_t opcode_execution_count_ = 0;
    std::size_t ast_fallback_executions_ = 0;
    std::size_t lightweight_calls_ = 0;       // Phase 1.1: calls via lightweight (no-env) path
    std::size_t string_intern_hits_ = 0;
    std::size_t string_intern_misses_ = 0;
    std::size_t local_binding_cache_hits_ = 0;
    std::size_t local_binding_cache_misses_ = 0;
    std::size_t global_binding_cache_hits_ = 0;
    std::size_t global_binding_cache_misses_ = 0;
    std::size_t callback_invocations_ = 0;
    std::size_t serialized_value_exports_ = 0;
    std::size_t deserialized_value_imports_ = 0;
    std::size_t allocation_pressure_bytes_ = 0;
    std::size_t young_allocation_pressure_bytes_ = 0;
    std::size_t incremental_trigger_bytes_ = kDefaultIncrementalTriggerBytes;
    std::size_t nursery_trigger_bytes_      = kDefaultNurseryTriggerBytes;
    std::size_t nursery_young_bytes_before_  = 0;  // Phase 3.5: nursery live bytes before young GC
    std::size_t young_promoted_bytes_this_cycle_ = 0; // Phase 3.5: bytes promoted in current young GC
    std::size_t promotion_survival_threshold_ = 2;
    std::size_t large_object_threshold_bytes_ = kDefaultLargeObjectThresholdBytes;
    bool gc_stress_enabled_ = false;
    std::size_t gc_stress_budget_work_ = 1;
    std::size_t young_collection_frequency_ = 3;
    std::uint64_t next_callback_id_ = 1;
    std::uint64_t next_coroutine_handle_id_ = 1;
    std::uint32_t current_frame_epoch_ = 0;
    std::uint32_t current_tick_epoch_ = 0;
    std::uint32_t current_scene_epoch_ = 0;
    std::vector<ZephyrBreakpoint> breakpoints_;
    std::size_t last_breakpoint_ip_ = 0;
    std::uint32_t last_breakpoint_line_ = 0;
    std::string last_breakpoint_module_;
    using ProfileClock = std::chrono::steady_clock;
    ProfileClock::time_point runtime_start_time_ = ProfileClock::now();
    struct ActiveProfileFrame {
        std::string function_name;
        ProfileClock::time_point start_time;
        std::uint64_t child_ns = 0;
    };
    std::unordered_map<std::string, ZephyrProfileEntry> profile_entries_;
    std::vector<ActiveProfileFrame> profile_stack_;
    std::deque<GCPauseRecord> pause_records_;
    std::vector<GCTraceEventRecord> gc_trace_events_;
    std::vector<CoroutineTraceEvent> coroutine_trace_events_;
    std::unordered_map<std::string, BytecodeCacheEntry> bytecode_cache_;
    bool gc_trace_active_ = false;
    bool coroutine_trace_active_ = false;
    bool bytecode_cache_enabled_ = false;
    bool full_gc_pause_active_ = false;
    ProfileClock::time_point full_gc_pause_start_{};
    std::size_t full_gc_heap_before_ = 0;
    std::uint64_t frame_budget_miss_count_ = 0;
    std::uint64_t next_coroutine_trace_id_ = 1;

    friend class ZephyrModuleBinder;
};

// Phase 1.2: Post-compilation bytecode optimizer — constant folding + peephole.
// Runs over the instruction vector and replaces known-constant patterns with
// pre-computed results.  All transformations are semantics-preserving.
namespace bytecode_cache {
inline void rebuild_opcode_histogram(BytecodeFunction& function);
}
inline void optimize_bytecode(BytecodeFunction& func);

inline int recompute_register_max(const BytecodeFunction& function) {
    int max_reg = std::max(function.local_count, 0);
    auto note_reg = [&](int reg) {
        if (reg >= 0) {
            max_reg = std::max(max_reg, reg + 1);
        }
    };

    for (const auto& instruction : function.instructions) {
        switch (instruction.op) {
            case BytecodeOp::R_LOAD_CONST:
            case BytecodeOp::R_LOAD_GLOBAL:
                note_reg(unpack_r_dst_operand(instruction.operand));
                break;
            case BytecodeOp::R_STORE_GLOBAL:
                note_reg(unpack_r_src_operand(instruction.operand));
                break;
            case BytecodeOp::R_MOVE:
            case BytecodeOp::R_NOT:
            case BytecodeOp::R_NEG:
            case BytecodeOp::R_RETURN:
            case BytecodeOp::R_YIELD:
                note_reg(instruction.dst);
                note_reg(instruction.src1);
                break;
            case BytecodeOp::R_CALL:
                note_reg(instruction.dst);
                note_reg(instruction.src1);
                for (std::uint8_t index = 0; index < instruction.operand_a; ++index) {
                    note_reg(static_cast<int>(instruction.src2) + index);
                }
                break;
            case BytecodeOp::R_JUMP_IF_FALSE:
            case BytecodeOp::R_JUMP_IF_TRUE:
                note_reg(unpack_r_src_operand(instruction.operand));
                break;
            case BytecodeOp::R_ADD:
            case BytecodeOp::R_SUB:
            case BytecodeOp::R_MUL:
            case BytecodeOp::R_DIV:
            case BytecodeOp::R_MOD:
            case BytecodeOp::R_LT:
            case BytecodeOp::R_LE:
            case BytecodeOp::R_GT:
            case BytecodeOp::R_GE:
            case BytecodeOp::R_EQ:
            case BytecodeOp::R_NE:
            case BytecodeOp::R_SI_ADD_STORE:
            case BytecodeOp::R_SI_SUB_STORE:
            case BytecodeOp::R_SI_MUL_STORE:
                note_reg(instruction.dst);
                note_reg(instruction.src1);
                note_reg(instruction.src2);
                break;
            case BytecodeOp::R_SI_CMP_JUMP_FALSE:
                note_reg(unpack_r_si_cmp_jump_false_src1(instruction.operand));
                note_reg(unpack_r_si_cmp_jump_false_src2(instruction.operand));
                break;
            case BytecodeOp::R_SI_LOAD_ADD_STORE:
                note_reg(unpack_r_si_load_add_store_dst(instruction.operand));
                note_reg(unpack_r_si_load_add_store_local_src(instruction.operand));
                break;
            default:
                break;
        }
    }

    return max_reg;
}

inline void optimize_register_bytecode(BytecodeFunction* func) {
    if (func == nullptr) {
        return;
    }
    for (auto& meta : func->metadata) {
        if (meta.bytecode != nullptr) {
            optimize_bytecode(*meta.bytecode);
        }
    }
    if (!func->uses_register_mode) {
        bytecode_cache::rebuild_opcode_histogram(*func);
        func->max_regs = recompute_register_max(*func);
        return;
    }

    auto& code = func->instructions;
    auto& metadata = func->metadata;
    func->superinstruction_fusions = 0;
    func->total_original_opcode_count = code.size();

    if (code.size() < 2) {
        bytecode_cache::rebuild_opcode_histogram(*func);
        func->max_regs = recompute_register_max(*func);
        return;
    }

    std::vector<bool> deleted(code.size(), false);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i + 1 < code.size(); ++i) {
            if (deleted[i]) {
                continue;
            }
            std::size_t j = i + 1;
            while (j < code.size() && deleted[j]) {
                ++j;
            }
            if (j >= code.size()) {
                break;
            }
            std::size_t k = j + 1;
            while (k < code.size() && deleted[k]) {
                ++k;
            }

            if ((code[i].op == BytecodeOp::R_ADD || code[i].op == BytecodeOp::R_SUB || code[i].op == BytecodeOp::R_MUL) &&
                code[j].op == BytecodeOp::R_MOVE &&
                code[j].src1 == code[i].dst) {
                switch (code[i].op) {
                    case BytecodeOp::R_ADD: code[i].op = BytecodeOp::R_SI_ADD_STORE; break;
                    case BytecodeOp::R_SUB: code[i].op = BytecodeOp::R_SI_SUB_STORE; break;
                    case BytecodeOp::R_MUL: code[i].op = BytecodeOp::R_SI_MUL_STORE; break;
                    default: break;
                }
                code[i].dst = code[j].dst;
                metadata[i] = metadata[j];
                deleted[j] = true;
                ++func->superinstruction_fusions;
                changed = true;
                continue;
            }

            if (is_register_comparison_op(code[i].op) &&
                code[j].op == BytecodeOp::R_JUMP_IF_FALSE &&
                unpack_r_src_operand(code[j].operand) == code[i].dst) {
                const auto kind = register_superinstruction_compare_kind(code[i].op);
                if (kind.has_value()) {
                    code[i].op = BytecodeOp::R_SI_CMP_JUMP_FALSE;
                    code[i].operand = pack_r_si_cmp_jump_false_operand(code[i].src1, code[i].src2, *kind);
                    metadata[i] = metadata[j];
                    metadata[i].jump_table = {unpack_r_jump_target_operand(code[j].operand)};
                    deleted[j] = true;
                    ++func->superinstruction_fusions;
                    changed = true;
                    continue;
                }
            }

            if (k < code.size() &&
                code[i].op == BytecodeOp::R_LOAD_CONST &&
                code[j].op == BytecodeOp::R_ADD &&
                code[k].op == BytecodeOp::R_MOVE &&
                code[k].src1 == code[j].dst) {
                const std::uint8_t const_reg = unpack_r_dst_operand(code[i].operand);
                const int constant_index = unpack_r_index_operand(code[i].operand);
                std::optional<std::uint8_t> local_src;
                if (code[j].src1 == const_reg && static_cast<int>(code[j].src2) < func->local_count) {
                    local_src = code[j].src2;
                } else if (code[j].src2 == const_reg && static_cast<int>(code[j].src1) < func->local_count) {
                    local_src = code[j].src1;
                }

                int packed_operand = 0;
                if (local_src.has_value() &&
                    static_cast<int>(code[k].dst) < func->local_count &&
                    try_pack_r_si_load_add_store_operand(code[k].dst, *local_src, constant_index, packed_operand)) {
                    code[i].op = BytecodeOp::R_SI_LOAD_ADD_STORE;
                    code[i].operand = packed_operand;
                    metadata[i] = metadata[k];
                    deleted[j] = true;
                    deleted[k] = true;
                    ++func->superinstruction_fusions;
                    changed = true;
                    continue;
                }
            }
        }
    }

    std::vector<int> index_map(code.size(), 0);
    int new_index = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        index_map[i] = new_index;
        if (!deleted[i]) {
            ++new_index;
        }
    }

    for (std::size_t i = 0; i < code.size(); ++i) {
        if (deleted[i]) {
            continue;
        }
        if (code[i].op == BytecodeOp::R_JUMP &&
            code[i].operand >= 0 &&
            static_cast<std::size_t>(code[i].operand) < index_map.size()) {
            code[i].operand = index_map[static_cast<std::size_t>(code[i].operand)];
            continue;
        }
        if ((code[i].op == BytecodeOp::R_JUMP_IF_FALSE || code[i].op == BytecodeOp::R_JUMP_IF_TRUE) &&
            unpack_r_jump_target_operand(code[i].operand) >= 0 &&
            static_cast<std::size_t>(unpack_r_jump_target_operand(code[i].operand)) < index_map.size()) {
            int packed_operand = 0;
            if (try_pack_r_cond_jump_operand(unpack_r_src_operand(code[i].operand),
                                             index_map[static_cast<std::size_t>(unpack_r_jump_target_operand(code[i].operand))],
                                             packed_operand)) {
                code[i].operand = packed_operand;
            }
            continue;
        }
        if (code[i].op == BytecodeOp::R_SI_CMP_JUMP_FALSE &&
            !metadata[i].jump_table.empty() &&
            metadata[i].jump_table.front() >= 0 &&
            static_cast<std::size_t>(metadata[i].jump_table.front()) < index_map.size()) {
            metadata[i].jump_table.front() = index_map[static_cast<std::size_t>(metadata[i].jump_table.front())];
        }
    }

    std::size_t write = 0;
    for (std::size_t read = 0; read < code.size(); ++read) {
        if (deleted[read]) {
            continue;
        }
        if (write != read) {
            code[write] = std::move(code[read]);
            metadata[write] = std::move(metadata[read]);
            if (read < func->line_table.size()) {
                func->line_table[write] = func->line_table[read];
            }
        }
        ++write;
    }
    code.resize(write);
    metadata.resize(write);
    if (func->line_table.size() > write) {
        func->line_table.resize(write);
    }

    bytecode_cache::rebuild_opcode_histogram(*func);
    func->max_regs = recompute_register_max(*func);
}

inline void optimize_bytecode(BytecodeFunction& func) {
    auto& code = func.instructions;
    auto& metadata = func.metadata;
    auto& constants = func.constants;
    if (func.uses_register_mode) {
        optimize_register_bytecode(&func);
        return;
    }
    if (code.size() < 2) return;
    func.superinstruction_fusions = 0;
    func.total_original_opcode_count = code.size();
    const bool allow_superinstructions = !func.is_coroutine_body;

    auto is_arith_op = [](BytecodeOp op) {
        return op == BytecodeOp::Add || op == BytecodeOp::Subtract ||
               op == BytecodeOp::Multiply || op == BytecodeOp::Divide || op == BytecodeOp::Modulo;
    };

    // Phase A: constant folding — replace in-place without removing instructions.
    // LoadConst A, LoadConst B, ArithOp → LoadConst result, Pop, Pop
    // We'll mark deleted instructions and compact at the end.
    std::vector<bool> deleted(code.size(), false);

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i + 1 < code.size(); ++i) {
            if (deleted[i]) continue;
            // Find next non-deleted after i
            std::size_t j = i + 1;
            while (j < code.size() && deleted[j]) ++j;
            if (j >= code.size()) break;
            std::size_t k = j + 1;
            while (k < code.size() && deleted[k]) ++k;
            std::size_t l = k + 1;
            while (l < code.size() && deleted[l]) ++l;

            // Binary fold: LoadConst, LoadConst, ArithOp
            if (k < code.size() &&
                code[i].op == BytecodeOp::LoadConst &&
                code[j].op == BytecodeOp::LoadConst &&
                is_arith_op(code[k].op)) {
                const auto& left = constants[static_cast<std::size_t>(code[i].operand)];
                const auto& right = constants[static_cast<std::size_t>(code[j].operand)];

                std::optional<BytecodeConstant> result;
                const auto* li = std::get_if<std::int64_t>(&left);
                const auto* lf = std::get_if<double>(&left);
                const auto* ri = std::get_if<std::int64_t>(&right);
                const auto* rf = std::get_if<double>(&right);

                if ((li || lf) && (ri || rf)) {
                    if (li && ri) {
                        switch (code[k].op) {
                            case BytecodeOp::Add:      result = BytecodeConstant{*li + *ri}; break;
                            case BytecodeOp::Subtract:  result = BytecodeConstant{*li - *ri}; break;
                            case BytecodeOp::Multiply:  result = BytecodeConstant{*li * *ri}; break;
                            case BytecodeOp::Divide:    if (*ri != 0) result = BytecodeConstant{*li / *ri}; break;
                            case BytecodeOp::Modulo:    if (*ri != 0) result = BytecodeConstant{*li % *ri}; break;
                            default: break;
                        }
                    } else {
                        const double lv = lf ? *lf : static_cast<double>(*li);
                        const double rv = rf ? *rf : static_cast<double>(*ri);
                        switch (code[k].op) {
                            case BytecodeOp::Add:      result = BytecodeConstant{lv + rv}; break;
                            case BytecodeOp::Subtract:  result = BytecodeConstant{lv - rv}; break;
                            case BytecodeOp::Multiply:  result = BytecodeConstant{lv * rv}; break;
                            case BytecodeOp::Divide:    result = BytecodeConstant{lv / rv}; break;
                            default: break;
                        }
                    }
                }
                // String concat fold
                if (code[k].op == BytecodeOp::Add) {
                    const auto* ls = std::get_if<std::string>(&left);
                    const auto* rs = std::get_if<std::string>(&right);
                    if (ls && rs) result = BytecodeConstant{*ls + *rs};
                }

                if (result.has_value()) {
                    constants.push_back(std::move(*result));
                    code[i].operand = static_cast<int>(constants.size() - 1);
                    deleted[j] = true;
                    deleted[k] = true;
                    changed = true;
                    continue;
                }
            }

            // Unary fold: LoadConst, Negate/Not
            if (k < code.size() &&
                code[i].op == BytecodeOp::LoadConst &&
                (code[j].op == BytecodeOp::Negate || code[j].op == BytecodeOp::Not)) {
                const auto& operand = constants[static_cast<std::size_t>(code[i].operand)];
                std::optional<BytecodeConstant> result;
                if (code[j].op == BytecodeOp::Negate) {
                    if (const auto* v = std::get_if<std::int64_t>(&operand)) result = BytecodeConstant{-*v};
                    if (const auto* v = std::get_if<double>(&operand)) result = BytecodeConstant{-*v};
                }
                if (code[j].op == BytecodeOp::Not) {
                    if (const auto* v = std::get_if<bool>(&operand)) result = BytecodeConstant{!*v};
                }
                if (result.has_value()) {
                    constants.push_back(std::move(*result));
                    code[i].operand = static_cast<int>(constants.size() - 1);
                    deleted[j] = true;
                    changed = true;
                    continue;
                }
            }

            // Peephole: Not, Not → delete both
            if (code[i].op == BytecodeOp::Not && code[j].op == BytecodeOp::Not) {
                deleted[i] = true;
                deleted[j] = true;
                changed = true;
                continue;
            }

            if (allow_superinstructions) {
                // Coroutine resumes are yield/resume heavy; keeping their dispatch loop free
                // of SI_* opcodes avoids paying switch/code-size cost where fusion does not help.
                if (l < code.size() &&
                    code[i].op == BytecodeOp::LoadLocal &&
                    code[j].op == BytecodeOp::LoadLocal &&
                    code[k].op == BytecodeOp::Add &&
                    code[l].op == BytecodeOp::StoreLocal) {
                    int packed_operand = 0;
                    if (try_pack_si_local_triple_operands(code[i].operand, code[j].operand, code[l].operand, packed_operand)) {
                        code[i].op = BytecodeOp::SILoadLocalAddStoreLocal;
                        code[i].operand = packed_operand;
                        metadata[i] = metadata[l];
                        ++func.superinstruction_fusions;
                        deleted[j] = true;
                        deleted[k] = true;
                        deleted[l] = true;
                        changed = true;
                        continue;
                    }
                }

                if (l < code.size() &&
                    code[i].op == BytecodeOp::LoadLocal &&
                    code[j].op == BytecodeOp::LoadConst &&
                    code[k].op == BytecodeOp::Add &&
                    code[l].op == BytecodeOp::StoreLocal) {
                    int packed_operand = 0;
                    if (try_pack_si_local_const_local_operands(code[i].operand, code[j].operand, code[l].operand, packed_operand)) {
                        code[i].op = BytecodeOp::SILoadLocalConstAddStoreLocal;
                        code[i].operand = packed_operand;
                        metadata[i] = metadata[l];
                        ++func.superinstruction_fusions;
                        deleted[j] = true;
                        deleted[k] = true;
                        deleted[l] = true;
                        changed = true;
                        continue;
                    }
                }

                if (l < code.size() &&
                    code[i].op == BytecodeOp::LoadLocal &&
                    code[j].op == BytecodeOp::LoadLocal &&
                    code[k].op == BytecodeOp::LoadConst &&
                    code[l].op == BytecodeOp::Modulo) {
                    int packed_operand = 0;
                    if (try_pack_si_local_local_const_operands(code[i].operand, code[j].operand, code[k].operand, packed_operand)) {
                        code[i].op = BytecodeOp::SILoadLocalLocalConstModulo;
                        code[i].operand = packed_operand;
                        ++func.superinstruction_fusions;
                        deleted[j] = true;
                        deleted[k] = true;
                        deleted[l] = true;
                        changed = true;
                        continue;
                    }
                }

                if (code[i].op == BytecodeOp::LoadLocal &&
                    code[j].op == BytecodeOp::LoadLocal) {
                    int packed_operand = 0;
                    if (try_pack_si_local_pair_operands(code[i].operand, code[j].operand, packed_operand)) {
                        code[i].op = BytecodeOp::SILoadLocalLoadLocal;
                        code[i].operand = packed_operand;
                        ++func.superinstruction_fusions;
                        deleted[j] = true;
                        changed = true;
                        continue;
                    }
                }

                if (code[i].op == BytecodeOp::Add &&
                    code[j].op == BytecodeOp::StoreLocal) {
                    code[i].op = BytecodeOp::SIAddStoreLocal;
                    code[i].operand = code[j].operand;
                    metadata[i] = metadata[j];
                    ++func.superinstruction_fusions;
                    deleted[j] = true;
                    changed = true;
                    continue;
                }

                if (code[i].op == BytecodeOp::LoadLocal &&
                    code[j].op == BytecodeOp::Add) {
                    code[i].op = BytecodeOp::SILoadLocalAdd;
                    ++func.superinstruction_fusions;
                    deleted[j] = true;
                    changed = true;
                    continue;
                }

                if (is_bytecode_comparison_op(code[i].op) &&
                    code[j].op == BytecodeOp::JumpIfFalse) {
                    const auto kind = superinstruction_compare_kind(code[i].op);
                    int packed_operand = 0;
                    if (kind.has_value() && try_pack_si_cmp_jump_operand(code[j].operand, *kind, packed_operand)) {
                        code[i].op = BytecodeOp::SICmpJumpIfFalse;
                        code[i].operand = packed_operand;
                        ++func.superinstruction_fusions;
                        deleted[j] = true;
                        changed = true;
                        continue;
                    }
                }
            }
        }
    }

    // Phase B: compact deleted instructions and fix jump targets.
    std::vector<int> index_map(code.size(), 0);
    int new_index = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        index_map[i] = new_index;
        if (!deleted[i]) ++new_index;
    }

    // Fix all jump targets.
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (deleted[i]) continue;
        if ((code[i].op == BytecodeOp::Jump ||
             code[i].op == BytecodeOp::JumpIfFalse ||
             code[i].op == BytecodeOp::JumpIfFalsePop ||
             code[i].op == BytecodeOp::JumpIfTrue) &&
            code[i].operand >= 0 &&
            static_cast<std::size_t>(code[i].operand) < index_map.size()) {
            code[i].operand = index_map[static_cast<std::size_t>(code[i].operand)];
        } else if (code[i].op == BytecodeOp::SICmpJumpIfFalse) {
            const int jump_target = unpack_si_cmp_jump_target(code[i].operand);
            if (jump_target >= 0 && static_cast<std::size_t>(jump_target) < index_map.size()) {
                int packed_operand = 0;
                const auto kind = superinstruction_compare_kind(unpack_si_cmp_jump_compare_op(code[i].operand));
                if (kind.has_value() && try_pack_si_cmp_jump_operand(index_map[static_cast<std::size_t>(jump_target)], *kind, packed_operand)) {
                    code[i].operand = packed_operand;
                }
            }
        }
    }

    // Compact instructions, metadata, and line_table.
    std::size_t write = 0;
    for (std::size_t read = 0; read < code.size(); ++read) {
        if (deleted[read]) continue;
        if (write != read) {
            code[write] = std::move(code[read]);
            metadata[write] = std::move(metadata[read]);
            if (read < func.line_table.size()) {
                func.line_table[write] = func.line_table[read];
            }
        }
        ++write;
    }
    code.resize(write);
    metadata.resize(write);
    if (func.line_table.size() > write) {
        func.line_table.resize(write);
    }

    // Rebuild opcode histogram.
    func.opcode_histogram.clear();
    for (const auto& instr : code) {
        ++func.opcode_histogram[bytecode_op_name(instr.op)];
    }

    // Recursively optimize nested functions.
    for (auto& meta : metadata) {
        if (meta.bytecode != nullptr) {
            optimize_bytecode(*meta.bytecode);
        }
    }
}

class BytecodeCompiler {
public:
    explicit BytecodeCompiler(BytecodeCompiler* parent = nullptr)
        : disable_register_mode_(parent != nullptr ? parent->disable_register_mode_ : false), parent_(parent) {}

    std::shared_ptr<BytecodeFunction> compile(const std::string& name, const std::vector<Param>& params, BlockStmt* body,
                                              bool is_coroutine_body = false, const std::vector<std::string>& generic_params = {}) {
        if (!is_coroutine_body && !disable_register_mode_) {
            if (auto compiled = try_compile_register_function(name, params, body, is_coroutine_body, generic_params); compiled != nullptr) {
                return compiled;
            }
        }
        return compile_stack_function(name, params, body, is_coroutine_body, generic_params);
    }

    std::shared_ptr<BytecodeFunction> compile_module(const std::string& name, Program* program) {
        reset_function_state(name);
        function_->global_slots_use_module_root_base = false;
        use_local_slots_ = false;
        allow_return_ = false;
        for (auto& statement : program->statements) {
            compile_stmt(statement.get());
        }
        emit_constant(BytecodeConstant{std::monostate{}}, Span{});
        emit(BytecodeOp::Return, Span{});
        function_->local_count = 0;
        optimize_bytecode(*function_);
        return function_;
    }

private:
    bool expr_contains_coroutine_constructs(Expr* expr) const {
        if (expr == nullptr) {
            return false;
        }
        if (dynamic_cast<CoroutineExpr*>(expr) != nullptr) {
            return true;
        }
        if (auto* function = dynamic_cast<FunctionExpr*>(expr)) {
            return block_contains_coroutine_constructs(function->body.get());
        }
        if (auto* array = dynamic_cast<ArrayExpr*>(expr)) {
            for (auto& element : array->elements) {
                if (expr_contains_coroutine_constructs(element.get())) {
                    return true;
                }
            }
            return false;
        }
        if (auto* group = dynamic_cast<GroupExpr*>(expr)) {
            return expr_contains_coroutine_constructs(group->inner.get());
        }
        if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            return expr_contains_coroutine_constructs(unary->right.get());
        }
        if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            return expr_contains_coroutine_constructs(binary->left.get()) || expr_contains_coroutine_constructs(binary->right.get());
        }
        if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
            return expr_contains_coroutine_constructs(assign->target.get()) || expr_contains_coroutine_constructs(assign->value.get());
        }
        if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
            return expr_contains_coroutine_constructs(member->object.get());
        }
        if (auto* optional_member = dynamic_cast<OptionalMemberExpr*>(expr)) {
            return expr_contains_coroutine_constructs(optional_member->object.get());
        }
        if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
            return expr_contains_coroutine_constructs(index->object.get()) || expr_contains_coroutine_constructs(index->index.get());
        }
        if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            if (expr_contains_coroutine_constructs(call->callee.get())) {
                return true;
            }
            for (auto& argument : call->arguments) {
                if (expr_contains_coroutine_constructs(argument.get())) {
                    return true;
                }
            }
            return false;
        }
        if (auto* optional_call = dynamic_cast<OptionalCallExpr*>(expr)) {
            if (expr_contains_coroutine_constructs(optional_call->object.get())) {
                return true;
            }
            for (auto& argument : optional_call->arguments) {
                if (expr_contains_coroutine_constructs(argument.get())) {
                    return true;
                }
            }
            return false;
        }
        if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
            return expr_contains_coroutine_constructs(resume->target.get());
        }
        if (auto* struct_init = dynamic_cast<StructInitExpr*>(expr)) {
            for (auto& field : struct_init->fields) {
                if (expr_contains_coroutine_constructs(field.value.get())) {
                    return true;
                }
            }
            return false;
        }
        if (auto* enum_init = dynamic_cast<EnumInitExpr*>(expr)) {
            for (auto& argument : enum_init->arguments) {
                if (expr_contains_coroutine_constructs(argument.get())) {
                    return true;
                }
            }
            return false;
        }
        if (auto* match_expr = dynamic_cast<MatchExpr*>(expr)) {
            if (expr_contains_coroutine_constructs(match_expr->subject.get())) {
                return true;
            }
            for (auto& arm : match_expr->arms) {
                if (arm.guard_expr != nullptr && expr_contains_coroutine_constructs(arm.guard_expr.get())) {
                    return true;
                }
                if (expr_contains_coroutine_constructs(arm.expression.get())) {
                    return true;
                }
            }
            return false;
        }
        return false;
    }

    bool stmt_contains_coroutine_constructs(Stmt* stmt) const {
        if (stmt == nullptr) {
            return false;
        }
        if (dynamic_cast<YieldStmt*>(stmt) != nullptr) {
            return true;
        }
        if (auto* let_stmt = dynamic_cast<LetStmt*>(stmt)) {
            return let_stmt->initializer != nullptr && expr_contains_coroutine_constructs(let_stmt->initializer.get());
        }
        if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
            return block_contains_coroutine_constructs(block);
        }
        if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
            return ((if_stmt->condition != nullptr && expr_contains_coroutine_constructs(if_stmt->condition.get())) ||
                    (if_stmt->let_subject != nullptr && expr_contains_coroutine_constructs(if_stmt->let_subject.get()))) ||
                   stmt_contains_coroutine_constructs(if_stmt->then_branch.get()) ||
                   (if_stmt->else_branch != nullptr && stmt_contains_coroutine_constructs(if_stmt->else_branch.get()));
        }
        if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
            return ((while_stmt->condition != nullptr && expr_contains_coroutine_constructs(while_stmt->condition.get())) ||
                    (while_stmt->let_subject != nullptr && expr_contains_coroutine_constructs(while_stmt->let_subject.get()))) ||
                   stmt_contains_coroutine_constructs(while_stmt->body.get());
        }
        if (auto* function_decl = dynamic_cast<FunctionDecl*>(stmt)) {
            return block_contains_coroutine_constructs(function_decl->body.get());
        }
        if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
            return return_stmt->value != nullptr && expr_contains_coroutine_constructs(return_stmt->value.get());
        }
        if (auto* expression_stmt = dynamic_cast<ExprStmt*>(stmt)) {
            return expr_contains_coroutine_constructs(expression_stmt->expression.get());
        }
        return false;
    }

    bool block_contains_coroutine_constructs(BlockStmt* block) const {
        if (block == nullptr) {
            return false;
        }
        for (auto& statement : block->statements) {
            if (stmt_contains_coroutine_constructs(statement.get())) {
                return true;
            }
        }
        return false;
    }

    bool program_contains_coroutine_constructs(Program* program) const {
        if (program == nullptr) {
            return false;
        }
        for (auto& statement : program->statements) {
            if (stmt_contains_coroutine_constructs(statement.get())) {
                return true;
            }
        }
        return false;
    }

    // Helper: Apply type variable substitution from type_bindings_ to a TypeRef
    TypeRef substitute_type(const TypeRef& type) const {
        if (type.parts.empty()) {
            return type;
        }
        // Check if the first part is a generic type parameter
        const auto& first_part = type.parts[0];
        const auto it = type_bindings_.find(first_part);
        if (it != type_bindings_.end()) {
            // If this is a generic parameter with a binding, use the bound type
            // For now, simple substitution: if T is bound, return the bound TypeRef
            return it->second;
        }
        // No substitution needed
        return type;
    }

    void reset_function_state(const std::string& name, bool is_coroutine_body = false, const std::vector<std::string>& generic_params = {}) {
        function_ = std::make_shared<BytecodeFunction>();
        function_->name = name;
        function_->is_coroutine_body = is_coroutine_body;
        use_local_slots_ = true;
        allow_return_ = true;
        has_env_ops_ = false;
        register_compile_failed_ = false;
        temp_counter_ = 0;
        next_slot_ = 0;
        cached_literal_regs_.clear();
        local_scopes_.clear();
        loop_stack_.clear();
        register_allocator_ = RegisterAllocator{};
        type_bindings_.clear();
        for (const auto& generic_param : generic_params) {
            type_bindings_[generic_param] = TypeRef{{generic_param}, Span{}};
        }
    }

    std::shared_ptr<BytecodeFunction> compile_stack_function(const std::string& name, const std::vector<Param>& params, BlockStmt* body,
                                                             bool is_coroutine_body, const std::vector<std::string>& generic_params = {}) {
        reset_function_state(name, is_coroutine_body, generic_params);
        enter_local_scope();
        for (const Param& param : params) {
            define_local_slot(param.name);
        }
        compile_block(body);
        emit_constant(BytecodeConstant{std::monostate{}}, body->span);
        emit(BytecodeOp::Return, body->span);
        function_->local_count = next_slot_;
        function_->uses_only_locals_and_upvalues = !has_env_ops_ && !function_->requires_full_closure;
        exit_local_scope();
        optimize_bytecode(*function_);
        return function_;
    }

    std::shared_ptr<BytecodeFunction> try_compile_register_function(const std::string& name,
                                                                    const std::vector<Param>& params,
                                                                    BlockStmt* body,
                                                                    bool is_coroutine_body,
                                                                    const std::vector<std::string>& generic_params = {}) {
        reset_function_state(name, is_coroutine_body, generic_params);
        function_->uses_register_mode = true;
        enter_local_scope();
        for (const Param& param : params) {
            define_local_slot(param.name);
        }
        register_allocator_.reserve_pinned(next_slot_);
        compile_block_r(body);
        if (!register_compile_failed_) {
            const std::uint8_t result_reg = compile_nil_r(body->span);
            emit_r(BytecodeOp::R_RETURN, body->span, 0, result_reg);
            register_allocator_.free_temp(result_reg);
        }
        function_->local_count = next_slot_;
        function_->max_regs = std::max(next_slot_, register_allocator_.max_regs);
        function_->uses_only_locals_and_upvalues = false;
        exit_local_scope();
        if (register_compile_failed_) {
            return nullptr;
        }
        optimize_bytecode(*function_);
        return function_;
    }
    void emit(BytecodeOp op, const Span& span, int operand = 0, const std::string& text = {}, std::optional<std::string> type_name = std::nullopt,
              bool flag = false, Expr* expr = nullptr, Stmt* stmt = nullptr, std::vector<std::string> names = {}, Pattern* pattern = nullptr,
              std::shared_ptr<BytecodeFunction> bytecode = {}) {
        CompactInstruction instruction;
        instruction.op = op;
        instruction.operand = static_cast<std::int32_t>(operand);
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        InstructionMetadata metadata;
        metadata.string_operand = text;
        metadata.type_name = std::move(type_name);
        metadata.flag = flag;
        metadata.names = std::move(names);
        metadata.expr = expr;
        metadata.stmt = stmt;
        metadata.pattern = pattern;
        metadata.bytecode = std::move(bytecode);
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.push_back(std::move(metadata));
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(op)];
        if (op == BytecodeOp::EvalAstExpr || op == BytecodeOp::ExecAstStmt) {
            function_->requires_full_closure = true;
        }
        if (op == BytecodeOp::LoadName || op == BytecodeOp::StoreName || op == BytecodeOp::DefineName || op == BytecodeOp::BindPattern ||
            op == BytecodeOp::EvalAstExpr || op == BytecodeOp::ExecAstStmt || op == BytecodeOp::DeclareTrait ||
            op == BytecodeOp::DeclareImpl || op == BytecodeOp::DeclareFunction || op == BytecodeOp::MakeFunction ||
            op == BytecodeOp::MakeCoroutine) {
            has_env_ops_ = true;
        }
    }

    int emit_jump(BytecodeOp op, const Span& span) {
        CompactInstruction instruction;
        instruction.op = op;
        instruction.operand = -1;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        return static_cast<int>(function_->instructions.size() - 1);
    }

    void patch_jump(int index) {
        function_->instructions[static_cast<std::size_t>(index)].operand = static_cast<std::int32_t>(function_->instructions.size());
    }

    void patch_jump_to(int index, int target) {
        function_->instructions[static_cast<std::size_t>(index)].operand = static_cast<std::int32_t>(target);
    }

    int emit_r_jump(BytecodeOp op, const Span& span, std::uint8_t src1 = 0) {
        CompactInstruction instruction;
        instruction.op = op;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        if (op == BytecodeOp::R_JUMP) {
            instruction.operand = -1;
        } else {
            int packed_operand = 0;
            if (!try_pack_r_cond_jump_operand(src1, 0x00FFFFFF, packed_operand)) {
                register_compile_failed_ = true;
            }
            instruction.operand = packed_operand;
        }
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(op)];
        return static_cast<int>(function_->instructions.size() - 1);
    }

    void patch_r_jump(int index) {
        patch_r_jump_to(index, static_cast<int>(function_->instructions.size()));
    }

    void patch_r_jump_to(int index, int target) {
        auto& instruction = function_->instructions[static_cast<std::size_t>(index)];
        if (instruction.op == BytecodeOp::R_JUMP) {
            instruction.operand = target;
            return;
        }
        int packed_operand = 0;
        if (!try_pack_r_cond_jump_operand(unpack_r_src_operand(instruction.operand), target, packed_operand)) {
            register_compile_failed_ = true;
            return;
        }
        instruction.operand = packed_operand;
    }

    struct LoopContext {
        std::string label;
        std::size_t scope_depth = 0;
        int continue_target = -1;
        std::vector<int> break_jumps;
        std::vector<int> continue_jumps;
    };

    void emit_constant(BytecodeConstant constant, const Span& span) {
        function_->constant_descriptions.push_back(describe_bytecode_constant_literal(constant));
        function_->constants.push_back(std::move(constant));
        emit(BytecodeOp::LoadConst, span, static_cast<int>(function_->constants.size() - 1));
    }

    int add_constant(BytecodeConstant constant) {
        function_->constant_descriptions.push_back(describe_bytecode_constant_literal(constant));
        function_->constants.push_back(std::move(constant));
        return static_cast<int>(function_->constants.size() - 1);
    }

    void emit_r(BytecodeOp op, const Span& span, std::uint8_t dst, std::uint8_t src1, std::uint8_t src2 = 0, std::uint8_t operand_a = 0) {
        CompactInstruction instruction;
        instruction.op = op;
        instruction.dst = dst;
        instruction.src1 = src1;
        instruction.src2 = src2;
        instruction.operand_a = operand_a;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(op)];
    }

    void emit_r_load_const(const Span& span, std::uint8_t dst, int const_index) {
        int packed_operand = 0;
        if (!try_pack_r_dst_index_operand(dst, const_index, packed_operand)) {
            register_compile_failed_ = true;
            return;
        }
        CompactInstruction instruction;
        instruction.op = BytecodeOp::R_LOAD_CONST;
        instruction.operand = packed_operand;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(BytecodeOp::R_LOAD_CONST)];
    }

    void emit_r_load_global(const Span& span, std::uint8_t dst, int global_slot) {
        int packed_operand = 0;
        if (!try_pack_r_dst_index_operand(dst, global_slot, packed_operand)) {
            register_compile_failed_ = true;
            return;
        }
        CompactInstruction instruction;
        instruction.op = BytecodeOp::R_LOAD_GLOBAL;
        instruction.operand = packed_operand;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(BytecodeOp::R_LOAD_GLOBAL)];
    }

    void emit_r_store_global(const Span& span, int global_slot, std::uint8_t src1) {
        int packed_operand = 0;
        if (!try_pack_r_src_index_operand(src1, global_slot, packed_operand)) {
            register_compile_failed_ = true;
            return;
        }
        CompactInstruction instruction;
        instruction.op = BytecodeOp::R_STORE_GLOBAL;
        instruction.operand = packed_operand;
        instruction.span_line = static_cast<std::uint32_t>(std::max<std::size_t>(1, span.line));
        function_->instructions.push_back(std::move(instruction));
        function_->metadata.emplace_back();
        function_->line_table.push_back(span.line);
        ++function_->opcode_histogram[bytecode_op_name(BytecodeOp::R_STORE_GLOBAL)];
    }

    void emit_r_binop(BytecodeOp op, const Span& span, std::uint8_t dst, std::uint8_t src1, std::uint8_t src2) {
        emit_r(op, span, dst, src1, src2);
    }

    std::string make_temp_name(const char* prefix) {
        return std::string(prefix) + std::to_string(temp_counter_++);
    }

    void collect_pattern_binding_names(Pattern* pattern, std::vector<std::string>& names) {
        if (pattern == nullptr) {
            return;
        }
        if (auto* binding = dynamic_cast<BindingPattern*>(pattern)) {
            if (std::find(names.begin(), names.end(), binding->name) == names.end()) {
                names.push_back(binding->name);
            }
            return;
        }
        if (auto* enum_pattern = dynamic_cast<EnumPattern*>(pattern)) {
            for (auto& payload : enum_pattern->payload) {
                collect_pattern_binding_names(payload.get(), names);
            }
            return;
        }
        if (auto* struct_pattern = dynamic_cast<StructPattern*>(pattern)) {
            for (auto& field : struct_pattern->fields) {
                collect_pattern_binding_names(field.pattern.get(), names);
            }
            return;
        }
        if (auto* array_pattern = dynamic_cast<ArrayPattern*>(pattern)) {
            for (auto& element : array_pattern->elements) {
                collect_pattern_binding_names(element.get(), names);
            }
            if (array_pattern->has_rest && !array_pattern->rest_name.empty() && array_pattern->rest_name != "_" &&
                std::find(names.begin(), names.end(), array_pattern->rest_name) == names.end()) {
                names.push_back(array_pattern->rest_name);
            }
            return;
        }
        if (auto* tuple_pattern = dynamic_cast<TuplePattern*>(pattern)) {
            for (auto& element : tuple_pattern->elements) {
                collect_pattern_binding_names(element.get(), names);
            }
            return;
        }
        if (auto* or_pattern = dynamic_cast<OrPattern*>(pattern)) {
            if (!or_pattern->alternatives.empty()) {
                collect_pattern_binding_names(or_pattern->alternatives.front().get(), names);
            }
        }
    }

    void enter_local_scope() {
        local_scopes_.emplace_back();
    }

    void exit_local_scope() {
        if (!local_scopes_.empty()) {
            local_scopes_.pop_back();
        }
    }

    void emit_scope_unwind_to_depth(std::size_t target_depth, const Span& span) {
        while (local_scopes_.size() > target_depth) {
            emit(BytecodeOp::ExitScope, span);
            target_depth += 1;
        }
    }

    LoopContext* current_loop(const std::string& label = {}) {
        if (loop_stack_.empty()) {
            return nullptr;
        }
        if (label.empty()) {
            return &loop_stack_.back();
        }
        for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it) {
            if (it->label == label) {
                return &(*it);
            }
        }
        return nullptr;
    }

    int define_local_slot(const std::string& name) {
        if (local_scopes_.empty()) {
            enter_local_scope();
        }
        const int slot = next_slot_++;
        local_scopes_.back()[name] = slot;
        if (static_cast<std::size_t>(slot) == function_->local_names.size()) {
            function_->local_names.push_back(name);
        } else if (static_cast<std::size_t>(slot) < function_->local_names.size()) {
            function_->local_names[static_cast<std::size_t>(slot)] = name;
        }
        return slot;
    }

    std::optional<int> resolve_local_slot(const std::string& name) const {
        for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    std::optional<int> find_upvalue_slot(const std::string& name) const {
        const auto it = std::find(function_->upvalue_names.begin(), function_->upvalue_names.end(), name);
        if (it == function_->upvalue_names.end()) {
            return std::nullopt;
        }
        return static_cast<int>(std::distance(function_->upvalue_names.begin(), it));
    }

    int add_upvalue_slot(const std::string& name) {
        if (const auto slot = find_upvalue_slot(name); slot.has_value()) {
            return *slot;
        }
        function_->upvalue_names.push_back(name);
        return static_cast<int>(function_->upvalue_names.size() - 1);
    }

    std::optional<int> find_global_slot(const std::string& name) const {
        const auto it = std::find(function_->global_names.begin(), function_->global_names.end(), name);
        if (it == function_->global_names.end()) {
            return std::nullopt;
        }
        return static_cast<int>(std::distance(function_->global_names.begin(), it));
    }

    int add_global_slot(const std::string& name) {
        if (const auto slot = find_global_slot(name); slot.has_value()) {
            return *slot;
        }
        function_->global_names.push_back(name);
        return static_cast<int>(function_->global_names.size() - 1);
    }

    std::optional<int> resolve_upvalue_slot(const std::string& name) {
        if (parent_ == nullptr) {
            return std::nullopt;
        }
        if (parent_->resolve_local_slot(name).has_value()) {
            return add_upvalue_slot(name);
        }
        if (parent_->resolve_upvalue_slot(name).has_value()) {
            return add_upvalue_slot(name);
        }
        return std::nullopt;
    }

    std::shared_ptr<BytecodeFunction> compile_nested_function(const std::string& name, const std::vector<Param>& params, BlockStmt* body,
                                                              bool is_coroutine_body = false) {
        BytecodeCompiler child(this);
        return child.compile(name, params, body, is_coroutine_body);
    }

    void emit_load_symbol(const std::string& name, const Span& span) {
        if (use_local_slots_) {
            if (const auto slot = resolve_local_slot(name); slot.has_value()) {
                emit(BytecodeOp::LoadLocal, span, *slot, name);
                return;
            }
            if (const auto upvalue = resolve_upvalue_slot(name); upvalue.has_value()) {
                emit(BytecodeOp::LoadUpvalue, span, *upvalue, name);
                return;
            }
        }
        emit(BytecodeOp::LoadName, span, add_global_slot(name), name);
    }

    void emit_define_symbol(const std::string& name, const Span& span, std::optional<std::string> type_name = std::nullopt,
                            bool mutable_value = false) {
        if (use_local_slots_) {
            const int slot = define_local_slot(name);
            emit(BytecodeOp::DefineLocal, span, slot, name, std::move(type_name), mutable_value);
            return;
        }
        emit(BytecodeOp::DefineName, span, add_global_slot(name), name, std::move(type_name), mutable_value);
    }

    void emit_store_symbol(const std::string& name, const Span& span) {
        if (use_local_slots_) {
            if (const auto slot = resolve_local_slot(name); slot.has_value()) {
                emit(BytecodeOp::StoreLocal, span, *slot, name);
                return;
            }
            if (const auto upvalue = resolve_upvalue_slot(name); upvalue.has_value()) {
                emit(BytecodeOp::StoreUpvalue, span, *upvalue, name);
                return;
            }
        }
        emit(BytecodeOp::StoreName, span, add_global_slot(name), name);
    }

    void emit_export_name(const std::string& name, const Span& span) {
        emit(BytecodeOp::ExportName, span, 0, name);
    }

    void fail_register_compile() {
        register_compile_failed_ = true;
    }

    std::uint8_t compile_nil_r(const Span& span) {
        const auto cached = cached_literal_regs_.find("nil");
        if (cached != cached_literal_regs_.end()) {
            return cached->second;
        }
        const std::uint8_t reg = register_allocator_.alloc_persistent();
        emit_r_load_const(span, reg, add_constant(BytecodeConstant{std::monostate{}}));
        cached_literal_regs_.emplace("nil", reg);
        return reg;
    }

    std::uint8_t ensure_register_result(std::uint8_t reg, const Span& span) {
        if (register_allocator_.is_temp(reg)) {
            return reg;
        }
        const std::uint8_t dst = register_allocator_.alloc_temp();
        emit_r(BytecodeOp::R_MOVE, span, dst, reg);
        return dst;
    }

    void emit_boolize_r(const Span& span, std::uint8_t dst, std::uint8_t src) {
        emit_r(BytecodeOp::R_NOT, span, dst, src);
        emit_r(BytecodeOp::R_NOT, span, dst, dst);
    }

    std::optional<std::uint8_t> emit_load_symbol_r(const std::string& name, const Span& span) {
        if (const auto slot = resolve_local_slot(name); slot.has_value()) {
            return static_cast<std::uint8_t>(*slot);
        }
        if (resolve_upvalue_slot(name).has_value()) {
            fail_register_compile();
            return std::nullopt;
        }
        const std::uint8_t reg = register_allocator_.alloc_temp();
        emit_r_load_global(span, reg, add_global_slot(name));
        return reg;
    }

    std::uint8_t compile_call_r(CallExpr* call) {
        if (function_->is_coroutine_body) {
            fail_register_compile();
            return 0;
        }
        if (dynamic_cast<MemberExpr*>(call->callee.get()) != nullptr) {
            fail_register_compile();
            return 0;
        }

        const auto callee_reg = compile_expr_r(call->callee.get());
        if (!callee_reg.has_value()) {
            return 0;
        }

        std::vector<std::uint8_t> args;
        args.reserve(call->arguments.size());
        for (auto& argument : call->arguments) {
            const auto arg_reg = compile_expr_r(argument.get());
            if (!arg_reg.has_value()) {
                register_allocator_.free_temp(*callee_reg);
                return 0;
            }
            args.push_back(*arg_reg);
        }

        std::uint8_t args_start = 0;
        if (!args.empty()) {
            args_start = register_allocator_.alloc_temp_block(args.size());
            for (std::size_t index = 0; index < args.size(); ++index) {
                emit_r(BytecodeOp::R_MOVE, call->span, static_cast<std::uint8_t>(args_start + index), args[index]);
            }
        }

        const std::uint8_t dst = register_allocator_.alloc_temp();
        emit_r(BytecodeOp::R_CALL,
               call->span,
               dst,
               *callee_reg,
               args_start,
               static_cast<std::uint8_t>(call->arguments.size()));

        for (std::uint8_t arg : args) {
            register_allocator_.free_temp(arg);
        }
        if (!args.empty()) {
            register_allocator_.free_temp_block(args_start, args.size());
        }
        register_allocator_.free_temp(*callee_reg);
        return dst;
    }

    std::optional<std::uint8_t> compile_expr_r(Expr* expr) {
        if (register_compile_failed_) {
            return std::nullopt;
        }
        if (auto* literal = dynamic_cast<LiteralExpr*>(expr)) {
            std::optional<std::string> literal_key;
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        literal_key = "nil";
                    } else if constexpr (std::is_same_v<T, bool>) {
                        literal_key = value ? "bool:true" : "bool:false";
                    } else if constexpr (std::is_same_v<T, std::int64_t>) {
                        if (value >= -1 && value <= 16) {
                            literal_key = "int:" + std::to_string(value);
                        }
                    }
                },
                literal->value);
            if (literal_key.has_value()) {
                const auto cached = cached_literal_regs_.find(*literal_key);
                if (cached != cached_literal_regs_.end()) {
                    return cached->second;
                }
            }

            const std::uint8_t reg = literal_key.has_value() ? register_allocator_.alloc_persistent() : register_allocator_.alloc_temp();
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        emit_r_load_const(expr->span, reg, add_constant(BytecodeConstant{std::monostate{}}));
                    } else {
                        emit_r_load_const(expr->span, reg, add_constant(BytecodeConstant{value}));
                    }
                },
                literal->value);
            if (literal_key.has_value()) {
                cached_literal_regs_.emplace(*literal_key, reg);
            }
            return reg;
        }
        if (auto* variable = dynamic_cast<VariableExpr*>(expr)) {
            return emit_load_symbol_r(variable->name, variable->span);
        }
        if (auto* group = dynamic_cast<GroupExpr*>(expr)) {
            return compile_expr_r(group->inner.get());
        }
        if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            if (unary->op == TokenType::Question) {
                fail_register_compile();
                return std::nullopt;
            }
            const auto src = compile_expr_r(unary->right.get());
            if (!src.has_value()) {
                return std::nullopt;
            }
            const std::uint8_t dst = register_allocator_.alloc_temp();
            if (unary->op == TokenType::Bang) {
                emit_r(BytecodeOp::R_NOT, unary->span, dst, *src);
            } else if (unary->op == TokenType::Minus) {
                emit_r(BytecodeOp::R_NEG, unary->span, dst, *src);
            } else {
                fail_register_compile();
            }
            register_allocator_.free_temp(*src);
            return register_compile_failed_ ? std::nullopt : std::optional<std::uint8_t>(dst);
        }
        if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            if (binary->op == TokenType::AndAnd || binary->op == TokenType::OrOr) {
                const auto left = compile_expr_r(binary->left.get());
                if (!left.has_value()) {
                    return std::nullopt;
                }
                const std::uint8_t dst = register_allocator_.alloc_temp();
                const int short_jump = emit_r_jump(binary->op == TokenType::AndAnd ? BytecodeOp::R_JUMP_IF_FALSE : BytecodeOp::R_JUMP_IF_TRUE,
                                                   binary->span,
                                                   *left);
                register_allocator_.free_temp(*left);

                const auto right = compile_expr_r(binary->right.get());
                if (!right.has_value()) {
                    return std::nullopt;
                }
                emit_boolize_r(binary->span, dst, *right);
                register_allocator_.free_temp(*right);
                const int end_jump = emit_r_jump(BytecodeOp::R_JUMP, binary->span);

                patch_r_jump(short_jump);
                emit_r_load_const(binary->span, dst, add_constant(BytecodeConstant{binary->op == TokenType::OrOr}));
                patch_r_jump(end_jump);
                return dst;
            }

            const auto left = compile_expr_r(binary->left.get());
            const auto right = compile_expr_r(binary->right.get());
            if (!left.has_value() || !right.has_value()) {
                if (left.has_value()) register_allocator_.free_temp(*left);
                if (right.has_value()) register_allocator_.free_temp(*right);
                return std::nullopt;
            }

            const std::uint8_t dst = register_allocator_.alloc_temp();
            switch (binary->op) {
                case TokenType::Plus: emit_r_binop(BytecodeOp::R_ADD, binary->span, dst, *left, *right); break;
                case TokenType::Minus: emit_r_binop(BytecodeOp::R_SUB, binary->span, dst, *left, *right); break;
                case TokenType::Star: emit_r_binop(BytecodeOp::R_MUL, binary->span, dst, *left, *right); break;
                case TokenType::Slash: emit_r_binop(BytecodeOp::R_DIV, binary->span, dst, *left, *right); break;
                case TokenType::Percent: emit_r_binop(BytecodeOp::R_MOD, binary->span, dst, *left, *right); break;
                case TokenType::EqualEqual: emit_r_binop(BytecodeOp::R_EQ, binary->span, dst, *left, *right); break;
                case TokenType::BangEqual: emit_r_binop(BytecodeOp::R_NE, binary->span, dst, *left, *right); break;
                case TokenType::Less: emit_r_binop(BytecodeOp::R_LT, binary->span, dst, *left, *right); break;
                case TokenType::LessEqual: emit_r_binop(BytecodeOp::R_LE, binary->span, dst, *left, *right); break;
                case TokenType::Greater: emit_r_binop(BytecodeOp::R_GT, binary->span, dst, *left, *right); break;
                case TokenType::GreaterEqual: emit_r_binop(BytecodeOp::R_GE, binary->span, dst, *left, *right); break;
                default:
                    fail_register_compile();
                    break;
            }
            register_allocator_.free_temp(*left);
            register_allocator_.free_temp(*right);
            return register_compile_failed_ ? std::nullopt : std::optional<std::uint8_t>(dst);
        }
        if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
            auto* variable = dynamic_cast<VariableExpr*>(assign->target.get());
            if (variable == nullptr) {
                fail_register_compile();
                return std::nullopt;
            }

            std::optional<std::uint8_t> value_reg;
            if (assign->assignment_op == TokenType::Equal) {
                value_reg = compile_expr_r(assign->value.get());
            } else {
                const auto op = compound_assignment_binary_op(assign->assignment_op);
                if (!op.has_value()) {
                    fail_register_compile();
                    return std::nullopt;
                }
                const auto left = emit_load_symbol_r(variable->name, variable->span);
                const auto right = compile_expr_r(assign->value.get());
                if (!left.has_value() || !right.has_value()) {
                    if (left.has_value()) register_allocator_.free_temp(*left);
                    if (right.has_value()) register_allocator_.free_temp(*right);
                    return std::nullopt;
                }
                const std::uint8_t dst = register_allocator_.alloc_temp();
                switch (*op) {
                    case TokenType::Plus: emit_r_binop(BytecodeOp::R_ADD, assign->span, dst, *left, *right); break;
                    case TokenType::Minus: emit_r_binop(BytecodeOp::R_SUB, assign->span, dst, *left, *right); break;
                    case TokenType::Star: emit_r_binop(BytecodeOp::R_MUL, assign->span, dst, *left, *right); break;
                    case TokenType::Slash: emit_r_binop(BytecodeOp::R_DIV, assign->span, dst, *left, *right); break;
                    case TokenType::Percent: emit_r_binop(BytecodeOp::R_MOD, assign->span, dst, *left, *right); break;
                    default:
                        fail_register_compile();
                        break;
                }
                register_allocator_.free_temp(*left);
                register_allocator_.free_temp(*right);
                value_reg = dst;
            }

            if (!value_reg.has_value()) {
                return std::nullopt;
            }
            if (const auto slot = resolve_local_slot(variable->name); slot.has_value()) {
                const std::uint8_t dst = static_cast<std::uint8_t>(*slot);
                if (*value_reg != dst) {
                    emit_r(BytecodeOp::R_MOVE, assign->span, dst, *value_reg);
                    register_allocator_.free_temp(*value_reg);
                }
                return dst;
            }
            emit_r_store_global(assign->span, add_global_slot(variable->name), *value_reg);
            return value_reg;
        }
        if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            return compile_call_r(call);
        }

        fail_register_compile();
        return std::nullopt;
    }

    void compile_stmt_r(Stmt* stmt) {
        if (register_compile_failed_) {
            return;
        }
        if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
            compile_block_r(block);
            return;
        }
        if (auto* let_stmt = dynamic_cast<LetStmt*>(stmt)) {
            if (let_stmt->pattern != nullptr) {
                fail_register_compile();
                return;
            }
            const auto value_reg = compile_expr_r(let_stmt->initializer.get());
            if (!value_reg.has_value()) {
                return;
            }
            const std::uint8_t dst = static_cast<std::uint8_t>(define_local_slot(let_stmt->name));
            register_allocator_.reserve_pinned(next_slot_);
            if (*value_reg != dst) {
                emit_r(BytecodeOp::R_MOVE, let_stmt->span, dst, *value_reg);
                register_allocator_.free_temp(*value_reg);
            }
            return;
        }
        if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
            if (if_stmt->let_pattern != nullptr) {
                fail_register_compile();
                return;
            }
            const auto condition = compile_expr_r(if_stmt->condition.get());
            if (!condition.has_value()) {
                return;
            }
            const int false_jump = emit_r_jump(BytecodeOp::R_JUMP_IF_FALSE, if_stmt->span, *condition);
            register_allocator_.free_temp(*condition);
            compile_stmt_r(if_stmt->then_branch.get());
            const int end_jump = emit_r_jump(BytecodeOp::R_JUMP, if_stmt->span);
            patch_r_jump(false_jump);
            if (if_stmt->else_branch) {
                compile_stmt_r(if_stmt->else_branch.get());
            }
            patch_r_jump(end_jump);
            return;
        }
        if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (while_stmt->let_pattern != nullptr) {
                fail_register_compile();
                return;
            }
            const int loop_start = static_cast<int>(function_->instructions.size());
            loop_stack_.push_back(LoopContext{while_stmt->label, local_scopes_.size(), loop_start});
            const auto condition = compile_expr_r(while_stmt->condition.get());
            if (!condition.has_value()) {
                return;
            }
            const int exit_jump = emit_r_jump(BytecodeOp::R_JUMP_IF_FALSE, while_stmt->span, *condition);
            register_allocator_.free_temp(*condition);
            compile_stmt_r(while_stmt->body.get());
            const int back_jump = emit_r_jump(BytecodeOp::R_JUMP, while_stmt->span);
            patch_r_jump_to(back_jump, loop_start);
            const auto loop_context = loop_stack_.back();
            loop_stack_.pop_back();
            for (const int jump_index : loop_context.continue_jumps) {
                patch_r_jump_to(jump_index, loop_start);
            }
            patch_r_jump(exit_jump);
            const int break_target = static_cast<int>(function_->instructions.size());
            for (const int jump_index : loop_context.break_jumps) {
                patch_r_jump_to(jump_index, break_target);
            }
            return;
        }
        if (auto* break_stmt = dynamic_cast<BreakStmt*>(stmt)) {
            auto* loop = current_loop(break_stmt->label);
            if (loop == nullptr) {
                fail_register_compile();
                return;
            }
            loop->break_jumps.push_back(emit_r_jump(BytecodeOp::R_JUMP, break_stmt->span));
            return;
        }
        if (auto* continue_stmt = dynamic_cast<ContinueStmt*>(stmt)) {
            auto* loop = current_loop(continue_stmt->label);
            if (loop == nullptr) {
                fail_register_compile();
                return;
            }
            if (loop->continue_target >= 0) {
                const int jump = emit_r_jump(BytecodeOp::R_JUMP, continue_stmt->span);
                patch_r_jump_to(jump, loop->continue_target);
            } else {
                loop->continue_jumps.push_back(emit_r_jump(BytecodeOp::R_JUMP, continue_stmt->span));
            }
            return;
        }
        if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
            std::optional<std::uint8_t> result = return_stmt->value ? compile_expr_r(return_stmt->value.get()) : compile_nil_r(return_stmt->span);
            if (!result.has_value()) {
                return;
            }
            emit_r(BytecodeOp::R_RETURN, return_stmt->span, 0, *result);
            register_allocator_.free_temp(*result);
            return;
        }
        if (auto* expression_stmt = dynamic_cast<ExprStmt*>(stmt)) {
            const auto result = compile_expr_r(expression_stmt->expression.get());
            if (result.has_value()) {
                register_allocator_.free_temp(*result);
            }
            return;
        }
        if (auto* yield_stmt = dynamic_cast<YieldStmt*>(stmt)) {
            const std::optional<std::uint8_t> result =
                yield_stmt->value ? compile_expr_r(yield_stmt->value.get()) : std::optional<std::uint8_t>(compile_nil_r(yield_stmt->span));
            if (!result.has_value()) {
                return;
            }
            emit_r(BytecodeOp::R_YIELD, yield_stmt->span, 0, *result);
            register_allocator_.free_temp(*result);
            return;
        }

        fail_register_compile();
    }

    void compile_block_r(BlockStmt* block) {
        enter_local_scope();
        for (auto& statement : block->statements) {
            compile_stmt_r(statement.get());
            if (register_compile_failed_) {
                break;
            }
        }
        exit_local_scope();
    }

    void compile_literal_pattern(LiteralPattern* pattern) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    emit_constant(BytecodeConstant{std::monostate{}}, pattern->span);
                } else {
                    emit_constant(BytecodeConstant{value}, pattern->span);
                }
            },
            pattern->value);
    }

    void compile_pattern(const std::string& subject_name, Pattern* pattern, std::vector<int>& failure_jumps) {
        if (dynamic_cast<WildcardPattern*>(pattern) != nullptr) {
            return;
        }
        if (auto* binding = dynamic_cast<BindingPattern*>(pattern)) {
            emit_load_symbol(subject_name, binding->span);
            emit_define_symbol(binding->name, binding->span);
            return;
        }
        if (auto* literal = dynamic_cast<LiteralPattern*>(pattern)) {
            emit_load_symbol(subject_name, literal->span);
            compile_literal_pattern(literal);
            emit(BytecodeOp::Equal, literal->span);
            failure_jumps.push_back(emit_jump(BytecodeOp::JumpIfFalsePop, literal->span));
            return;
        }
        if (auto* enum_pattern = dynamic_cast<EnumPattern*>(pattern)) {
            emit_load_symbol(subject_name, enum_pattern->span);
            emit(BytecodeOp::IsEnumVariant,
                 enum_pattern->span,
                 static_cast<int>(enum_pattern->payload.size()),
                 enum_pattern->enum_name.display_name(),
                 enum_pattern->variant_name);
            failure_jumps.push_back(emit_jump(BytecodeOp::JumpIfFalsePop, enum_pattern->span));

            for (std::size_t i = 0; i < enum_pattern->payload.size(); ++i) {
                const std::string payload_name = make_temp_name("__zephyr_match_payload_");
                emit_load_symbol(subject_name, enum_pattern->span);
                emit(BytecodeOp::LoadEnumPayload, enum_pattern->span, static_cast<int>(i));
                emit_define_symbol(payload_name, enum_pattern->span);
                compile_pattern(payload_name, enum_pattern->payload[i].get(), failure_jumps);
            }
            return;
        }

        emit(BytecodeOp::MatchFail, pattern->span, 0, "Unsupported match pattern.");
    }

    void compile_stmt(Stmt* stmt) {
        if (auto* import_stmt = dynamic_cast<ImportStmt*>(stmt)) {
            emit(BytecodeOp::ImportModule, import_stmt->span, 0, import_stmt->path, import_stmt->alias);
            return;
        }
        if (auto* let_stmt = dynamic_cast<LetStmt*>(stmt)) {
            if (let_stmt->pattern != nullptr) {
                compile_expr(let_stmt->initializer.get());

                std::vector<std::string> binding_names;
                std::vector<std::int32_t> binding_slots;
                collect_pattern_binding_names(let_stmt->pattern.get(), binding_names);
                if (use_local_slots_) {
                    binding_slots.reserve(binding_names.size());
                    for (const auto& binding_name : binding_names) {
                        binding_slots.push_back(define_local_slot(binding_name));
                    }
                }

                emit(BytecodeOp::BindPattern,
                     let_stmt->span,
                     0,
                     {},
                     std::nullopt,
                     false,
                     nullptr,
                     nullptr,
                     std::move(binding_names),
                     let_stmt->pattern.get());
                function_->metadata.back().jump_table = std::move(binding_slots);

                const int fail_jump = emit_jump(BytecodeOp::JumpIfFalse, let_stmt->span);
                emit(BytecodeOp::Pop, let_stmt->span);
                const int end_jump = emit_jump(BytecodeOp::Jump, let_stmt->span);

                patch_jump(fail_jump);
                emit(BytecodeOp::Pop, let_stmt->span);
                if (let_stmt->else_branch) {
                    compile_stmt(let_stmt->else_branch.get());
                } else {
                    emit(BytecodeOp::Fail, let_stmt->span, 0, "let pattern did not match.");
                }
                patch_jump(end_jump);
                return;
            }

            compile_expr(let_stmt->initializer.get());
            emit_define_symbol(let_stmt->name,
                               let_stmt->span,
                               let_stmt->type.has_value() ? std::optional<std::string>(let_stmt->type->display_name()) : std::nullopt,
                               let_stmt->mutable_value);
            if (stmt->exported) {
                emit_export_name(let_stmt->name, let_stmt->span);
            }
            return;
        }
        if (auto* function_decl = dynamic_cast<FunctionDecl*>(stmt)) {
            emit(BytecodeOp::DeclareFunction,
                 function_decl->span,
                 0,
                 function_decl->name,
                 function_decl->return_type.has_value() ? std::optional<std::string>(function_decl->return_type->display_name())
                                                        : std::nullopt,
                 false,
                 nullptr,
                 stmt,
                 encode_param_metadata(function_decl->params),
                 nullptr,
                 compile_nested_function(function_decl->name, function_decl->params, function_decl->body.get()));
            if (stmt->exported) {
                emit_export_name(function_decl->name, function_decl->span);
            }
            return;
        }
        if (auto* struct_decl = dynamic_cast<StructDecl*>(stmt)) {
            emit(BytecodeOp::DeclareStruct, struct_decl->span, 0, struct_decl->name, std::nullopt, false, nullptr, stmt);
            if (stmt->exported) {
                emit_export_name(struct_decl->name, struct_decl->span);
            }
            return;
        }
        if (auto* enum_decl = dynamic_cast<EnumDecl*>(stmt)) {
            emit(BytecodeOp::DeclareEnum, enum_decl->span, 0, enum_decl->name, std::nullopt, false, nullptr, stmt);
            if (stmt->exported) {
                emit_export_name(enum_decl->name, enum_decl->span);
            }
            return;
        }
        if (auto* trait_decl = dynamic_cast<TraitDecl*>(stmt)) {
            emit(BytecodeOp::DeclareTrait, trait_decl->span, 0, trait_decl->name, std::nullopt, false, nullptr, stmt);
            if (stmt->exported) {
                emit_export_name(trait_decl->name, trait_decl->span);
            }
            return;
        }
        if (auto* impl_decl = dynamic_cast<ImplDecl*>(stmt)) {
            emit(BytecodeOp::DeclareImpl, impl_decl->span, 0, impl_decl->trait_name.display_name(), std::nullopt, false, nullptr, stmt);
            return;
        }
        if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
            compile_block(block);
            return;
        }
        if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
            if (if_stmt->let_pattern != nullptr) {
                const std::string subject_name = make_temp_name("__zephyr_iflet_subject_");

                enter_local_scope();
                emit(BytecodeOp::EnterScope, if_stmt->span);
                compile_expr(if_stmt->let_subject.get());
                emit_define_symbol(subject_name, if_stmt->span);

                enter_local_scope();
                emit(BytecodeOp::EnterScope, if_stmt->span);

                std::vector<std::string> binding_names;
                std::vector<std::int32_t> binding_slots;
                collect_pattern_binding_names(if_stmt->let_pattern.get(), binding_names);
                if (use_local_slots_) {
                    binding_slots.reserve(binding_names.size());
                    for (const auto& binding_name : binding_names) {
                        binding_slots.push_back(define_local_slot(binding_name));
                    }
                }

                emit_load_symbol(subject_name, if_stmt->span);
                emit(BytecodeOp::BindPattern,
                     if_stmt->span,
                     0,
                     {},
                     std::nullopt,
                     false,
                     nullptr,
                     nullptr,
                     std::move(binding_names),
                     if_stmt->let_pattern.get());
                function_->metadata.back().jump_table = std::move(binding_slots);
                const int false_jump = emit_jump(BytecodeOp::JumpIfFalsePop, if_stmt->span);

                compile_stmt(if_stmt->then_branch.get());
                emit(BytecodeOp::ExitScope, if_stmt->span);
                exit_local_scope();
                const int end_jump = emit_jump(BytecodeOp::Jump, if_stmt->span);

                const int else_target = static_cast<int>(function_->instructions.size());
                patch_jump_to(false_jump, else_target);
                emit(BytecodeOp::ExitScope, if_stmt->span);
                exit_local_scope();

                if (if_stmt->else_branch) {
                    compile_stmt(if_stmt->else_branch.get());
                }
                patch_jump(end_jump);

                emit(BytecodeOp::ExitScope, if_stmt->span);
                exit_local_scope();
                return;
            }

            compile_expr(if_stmt->condition.get());
            const int false_jump = emit_jump(BytecodeOp::JumpIfFalse, if_stmt->span);
            emit(BytecodeOp::Pop, if_stmt->span);
            compile_stmt(if_stmt->then_branch.get());
            const int end_jump = emit_jump(BytecodeOp::Jump, if_stmt->span);
            patch_jump(false_jump);
            emit(BytecodeOp::Pop, if_stmt->span);
            if (if_stmt->else_branch) {
                compile_stmt(if_stmt->else_branch.get());
            }
            patch_jump(end_jump);
            return;
        }
        if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (while_stmt->let_pattern != nullptr) {
                const std::string subject_name = make_temp_name("__zephyr_whilelet_subject_");
                const int loop_start = static_cast<int>(function_->instructions.size());
                loop_stack_.push_back(LoopContext{while_stmt->label, local_scopes_.size(), -1});

                enter_local_scope();
                emit(BytecodeOp::EnterScope, while_stmt->span);
                compile_expr(while_stmt->let_subject.get());
                emit_define_symbol(subject_name, while_stmt->span);

                enter_local_scope();
                emit(BytecodeOp::EnterScope, while_stmt->span);

                std::vector<std::string> binding_names;
                std::vector<std::int32_t> binding_slots;
                collect_pattern_binding_names(while_stmt->let_pattern.get(), binding_names);
                if (use_local_slots_) {
                    binding_slots.reserve(binding_names.size());
                    for (const auto& binding_name : binding_names) {
                        binding_slots.push_back(define_local_slot(binding_name));
                    }
                }

                emit_load_symbol(subject_name, while_stmt->span);
                emit(BytecodeOp::BindPattern,
                     while_stmt->span,
                     0,
                     {},
                     std::nullopt,
                     false,
                     nullptr,
                     nullptr,
                     std::move(binding_names),
                     while_stmt->let_pattern.get());
                function_->metadata.back().jump_table = std::move(binding_slots);
                const int exit_jump = emit_jump(BytecodeOp::JumpIfFalsePop, while_stmt->span);

                compile_stmt(while_stmt->body.get());

                emit(BytecodeOp::ExitScope, while_stmt->span);
                exit_local_scope();
                emit(BytecodeOp::ExitScope, while_stmt->span);
                exit_local_scope();

                const int back_jump = emit_jump(BytecodeOp::Jump, while_stmt->span);
                patch_jump_to(back_jump, loop_start);

                const auto loop_context = loop_stack_.back();
                loop_stack_.pop_back();
                for (const int jump_index : loop_context.continue_jumps) {
                    patch_jump_to(jump_index, loop_start);
                }

                const int loop_exit_target = static_cast<int>(function_->instructions.size());
                patch_jump_to(exit_jump, loop_exit_target);
                emit(BytecodeOp::ExitScope, while_stmt->span);
                exit_local_scope();
                emit(BytecodeOp::ExitScope, while_stmt->span);
                exit_local_scope();

                const int break_target = static_cast<int>(function_->instructions.size());
                for (const int jump_index : loop_context.break_jumps) {
                    patch_jump_to(jump_index, break_target);
                }
                return;
            }

            const int loop_start = static_cast<int>(function_->instructions.size());
            loop_stack_.push_back(LoopContext{while_stmt->label, local_scopes_.size(), loop_start});
            compile_expr(while_stmt->condition.get());
            const int exit_jump = emit_jump(BytecodeOp::JumpIfFalse, while_stmt->span);
            emit(BytecodeOp::Pop, while_stmt->span);
            compile_stmt(while_stmt->body.get());
            emit(BytecodeOp::Jump, while_stmt->span, loop_start);
            const auto loop_context = loop_stack_.back();
            loop_stack_.pop_back();
            patch_jump(exit_jump);
            emit(BytecodeOp::Pop, while_stmt->span);
            const int break_target = static_cast<int>(function_->instructions.size());
            for (const int jump_index : loop_context.break_jumps) {
                patch_jump_to(jump_index, break_target);
            }
            return;
        }
        if (auto* for_stmt = dynamic_cast<ForStmt*>(stmt)) {
            const std::string iter_name = make_temp_name("__zephyr_for_iter_");
            const std::string index_name = make_temp_name("__zephyr_for_index_");

            enter_local_scope();
            emit(BytecodeOp::EnterScope, for_stmt->span);
            compile_expr(for_stmt->iterable.get());
            emit_define_symbol(iter_name, for_stmt->span);
            emit_constant(BytecodeConstant{static_cast<std::int64_t>(0)}, for_stmt->span);
            emit_define_symbol(index_name, for_stmt->span, std::string("Int"), true);

            const int loop_start = static_cast<int>(function_->instructions.size());
            loop_stack_.push_back(LoopContext{for_stmt->label, local_scopes_.size(), -1});
            emit_load_symbol(index_name, for_stmt->span);
            emit_load_symbol(iter_name, for_stmt->span);
            emit(BytecodeOp::ArrayLength, for_stmt->span);
            emit(BytecodeOp::Less, for_stmt->span);
            const int exit_jump = emit_jump(BytecodeOp::JumpIfFalse, for_stmt->span);
            emit(BytecodeOp::Pop, for_stmt->span);

            enter_local_scope();
            emit(BytecodeOp::EnterScope, for_stmt->span);
            emit_load_symbol(iter_name, for_stmt->span);
            emit_load_symbol(index_name, for_stmt->span);
            emit(BytecodeOp::LoadIndex, for_stmt->span);
            emit_define_symbol(for_stmt->name, for_stmt->span, std::nullopt, true);
            for (auto& statement : for_stmt->body->statements) {
                compile_stmt(statement.get());
            }
            emit(BytecodeOp::ExitScope, for_stmt->span);
            exit_local_scope();

            loop_stack_.back().continue_target = static_cast<int>(function_->instructions.size());
            emit_load_symbol(index_name, for_stmt->span);
            emit_constant(BytecodeConstant{static_cast<std::int64_t>(1)}, for_stmt->span);
            emit(BytecodeOp::Add, for_stmt->span);
            emit_store_symbol(index_name, for_stmt->span);
            emit(BytecodeOp::Pop, for_stmt->span);
            emit(BytecodeOp::Jump, for_stmt->span, loop_start);

            const auto loop_context = loop_stack_.back();
            loop_stack_.pop_back();
            for (const int jump_index : loop_context.continue_jumps) {
                patch_jump_to(jump_index, loop_context.continue_target);
            }
            patch_jump(exit_jump);
            emit(BytecodeOp::Pop, for_stmt->span);
            const int break_target = static_cast<int>(function_->instructions.size());
            for (const int jump_index : loop_context.break_jumps) {
                patch_jump_to(jump_index, break_target);
            }
            emit(BytecodeOp::ExitScope, for_stmt->span);
            exit_local_scope();
            return;
        }
        if (auto* break_stmt = dynamic_cast<BreakStmt*>(stmt)) {
            auto* loop = current_loop(break_stmt->label);
            if (loop == nullptr) {
                emit(BytecodeOp::Fail, break_stmt->span, 0, "break used outside loop.");
                return;
            }
            emit_scope_unwind_to_depth(loop->scope_depth, break_stmt->span);
            loop->break_jumps.push_back(emit_jump(BytecodeOp::Jump, break_stmt->span));
            return;
        }
        if (auto* continue_stmt = dynamic_cast<ContinueStmt*>(stmt)) {
            auto* loop = current_loop(continue_stmt->label);
            if (loop == nullptr) {
                emit(BytecodeOp::Fail, continue_stmt->span, 0, "continue used outside loop.");
                return;
            }
            emit_scope_unwind_to_depth(loop->scope_depth, continue_stmt->span);
            if (loop->continue_target >= 0) {
                emit(BytecodeOp::Jump, continue_stmt->span, loop->continue_target);
            } else {
                loop->continue_jumps.push_back(emit_jump(BytecodeOp::Jump, continue_stmt->span));
            }
            return;
        }
        if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (!allow_return_) {
                emit(BytecodeOp::Fail, return_stmt->span, 0, "top-level return is not allowed.");
                return;
            }
            if (return_stmt->value) {
                compile_expr(return_stmt->value.get());
            } else {
                emit_constant(BytecodeConstant{std::monostate{}}, return_stmt->span);
            }
            emit(BytecodeOp::Return, return_stmt->span);
            return;
        }
        if (auto* yield_stmt = dynamic_cast<YieldStmt*>(stmt)) {
            if (yield_stmt->value) {
                compile_expr(yield_stmt->value.get());
            } else {
                emit_constant(BytecodeConstant{std::monostate{}}, yield_stmt->span);
            }
            emit(BytecodeOp::Yield, yield_stmt->span);
            return;
        }
        if (auto* expression_stmt = dynamic_cast<ExprStmt*>(stmt)) {
            compile_expr(expression_stmt->expression.get());
            emit(BytecodeOp::Pop, expression_stmt->span);
            return;
        }
        emit(BytecodeOp::Fail, stmt->span, 0, "Unsupported statement node in bytecode compiler.");
    }

    void compile_block(BlockStmt* block) {
        enter_local_scope();
        emit(BytecodeOp::EnterScope, block->span);
        for (auto& statement : block->statements) {
            compile_stmt(statement.get());
        }
        emit(BytecodeOp::ExitScope, block->span);
        exit_local_scope();
    }

    void compile_expr(Expr* expr) {
        if (auto* literal = dynamic_cast<LiteralExpr*>(expr)) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        emit_constant(BytecodeConstant{std::monostate{}}, expr->span);
                    } else {
                        emit_constant(BytecodeConstant{value}, expr->span);
                    }
                },
                literal->value);
            return;
        }
        if (auto* interpolated = dynamic_cast<InterpolatedStringExpr*>(expr)) {
            if (interpolated->segments.empty()) {
                emit_constant(BytecodeConstant{std::string{}}, interpolated->span);
                return;
            }

            bool first_segment = true;
            for (auto& segment : interpolated->segments) {
                if (segment.is_literal) {
                    emit_constant(BytecodeConstant{segment.literal}, interpolated->span);
                } else {
                    compile_expr(segment.expression.get());
                    emit(BytecodeOp::Stringify, interpolated->span);
                }

                if (!first_segment) {
                    emit(BytecodeOp::Add, interpolated->span);
                }
                first_segment = false;
            }
            return;
        }
        if (auto* variable = dynamic_cast<VariableExpr*>(expr)) {
            emit_load_symbol(variable->name, variable->span);
            return;
        }
        if (auto* function = dynamic_cast<FunctionExpr*>(expr)) {
            emit(BytecodeOp::MakeFunction,
                 function->span,
                 0,
                 "<anonymous>",
                 function->return_type.has_value() ? std::optional<std::string>(function->return_type->display_name()) : std::nullopt,
                 false,
                 function,
                 nullptr,
                 encode_param_metadata(function->params),
                 nullptr,
                 compile_nested_function("<anonymous>", function->params, function->body.get()));
            return;
        }
        if (auto* array = dynamic_cast<ArrayExpr*>(expr)) {
            for (auto& element : array->elements) {
                compile_expr(element.get());
            }
            emit(BytecodeOp::BuildArray, array->span, static_cast<int>(array->elements.size()));
            return;
        }
        if (auto* group = dynamic_cast<GroupExpr*>(expr)) {
            compile_expr(group->inner.get());
            return;
        }
        if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            if (unary->op == TokenType::Question) {
                // Error propagation lowering:
                // 1. Evaluate the Result-like value
                // 2. If Ok(v), push v onto stack and continue
                // 3. If Err(e), return the entire Result::Err(e) immediately (propagate error)
                const std::string subject_name = make_temp_name("__zephyr_try_subject_");
                compile_expr(unary->right.get());
                emit_define_symbol(subject_name, unary->span);

                // Check for Ok variant
                emit_load_symbol(subject_name, unary->span);
                emit(BytecodeOp::IsEnumVariant, unary->span, 1, "Result", "Ok");
                const int ok_miss_jump = emit_jump(BytecodeOp::JumpIfFalsePop, unary->span);
                // Ok case: load and push the payload
                emit_load_symbol(subject_name, unary->span);
                emit(BytecodeOp::LoadEnumPayload, unary->span, 0);
                const int end_jump = emit_jump(BytecodeOp::Jump, unary->span);

                // Check for Err variant
                const int err_check = static_cast<int>(function_->instructions.size());
                patch_jump_to(ok_miss_jump, err_check);
                emit_load_symbol(subject_name, unary->span);
                emit(BytecodeOp::IsEnumVariant, unary->span, 1, "Result", "Err");
                const int not_err_jump = emit_jump(BytecodeOp::JumpIfFalsePop, unary->span);
                // Err case: return the entire Result::Err value (propagate error)
                emit_load_symbol(subject_name, unary->span);
                emit(BytecodeOp::Return, unary->span);

                // Invalid case: neither Ok nor Err
                const int bad_case = static_cast<int>(function_->instructions.size());
                patch_jump_to(not_err_jump, bad_case);
                emit(BytecodeOp::Fail, unary->span, 0, "'?': expects Result::Ok/Err.");

                const int end_target = static_cast<int>(function_->instructions.size());
                patch_jump_to(end_jump, end_target);
                return;
            }
            compile_expr(unary->right.get());
            if (unary->op == TokenType::Bang) {
                emit(BytecodeOp::Not, unary->span);
            } else if (unary->op == TokenType::Minus) {
                emit(BytecodeOp::Negate, unary->span);
            } else {
                emit(BytecodeOp::Fail, unary->span, 0, "Unsupported unary operator in bytecode compiler.");
            }
            return;
        }
        if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            if (binary->op == TokenType::AndAnd) {
                compile_expr(binary->left.get());
                const int false_jump = emit_jump(BytecodeOp::JumpIfFalse, binary->span);
                emit(BytecodeOp::Pop, binary->span);
                compile_expr(binary->right.get());
                emit(BytecodeOp::ToBool, binary->span);
                const int end_jump = emit_jump(BytecodeOp::Jump, binary->span);
                patch_jump(false_jump);
                emit(BytecodeOp::Pop, binary->span);
                emit_constant(BytecodeConstant{false}, binary->span);
                patch_jump(end_jump);
                return;
            }
            if (binary->op == TokenType::OrOr) {
                compile_expr(binary->left.get());
                const int true_jump = emit_jump(BytecodeOp::JumpIfTrue, binary->span);
                emit(BytecodeOp::Pop, binary->span);
                compile_expr(binary->right.get());
                emit(BytecodeOp::ToBool, binary->span);
                const int end_jump = emit_jump(BytecodeOp::Jump, binary->span);
                patch_jump(true_jump);
                emit(BytecodeOp::Pop, binary->span);
                emit_constant(BytecodeConstant{true}, binary->span);
                patch_jump(end_jump);
                return;
            }
            compile_expr(binary->left.get());
            compile_expr(binary->right.get());
            switch (binary->op) {
                case TokenType::Plus: emit(BytecodeOp::Add, binary->span); return;
                case TokenType::Minus: emit(BytecodeOp::Subtract, binary->span); return;
                case TokenType::Star: emit(BytecodeOp::Multiply, binary->span); return;
                case TokenType::Slash: emit(BytecodeOp::Divide, binary->span); return;
                case TokenType::Percent: emit(BytecodeOp::Modulo, binary->span); return;
                case TokenType::EqualEqual: emit(BytecodeOp::Equal, binary->span); return;
                case TokenType::BangEqual: emit(BytecodeOp::NotEqual, binary->span); return;
                case TokenType::Less: emit(BytecodeOp::Less, binary->span); return;
                case TokenType::LessEqual: emit(BytecodeOp::LessEqual, binary->span); return;
                case TokenType::Greater: emit(BytecodeOp::Greater, binary->span); return;
                case TokenType::GreaterEqual: emit(BytecodeOp::GreaterEqual, binary->span); return;
                default:
                    emit(BytecodeOp::Fail, binary->span, 0, "Unsupported binary operator in bytecode compiler.");
                    return;
            }
        }
        if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
            if (auto* variable = dynamic_cast<VariableExpr*>(assign->target.get())) {
                if (assign->assignment_op != TokenType::Equal) {
                    emit_load_symbol(variable->name, variable->span);
                    compile_expr(assign->value.get());
                    switch (*compound_assignment_binary_op(assign->assignment_op)) {
                        case TokenType::Plus:
                            emit(BytecodeOp::Add, assign->span);
                            break;
                        case TokenType::Minus:
                            emit(BytecodeOp::Subtract, assign->span);
                            break;
                        case TokenType::Star:
                            emit(BytecodeOp::Multiply, assign->span);
                            break;
                        case TokenType::Slash:
                            emit(BytecodeOp::Divide, assign->span);
                            break;
                        default:
                            emit(BytecodeOp::Fail, assign->span, 0, "Unsupported compound assignment operator.");
                            return;
                    }
                } else {
                    compile_expr(assign->value.get());
                }
                emit_store_symbol(variable->name, variable->span);
                return;
            }
            if (auto* member = dynamic_cast<MemberExpr*>(assign->target.get())) {
                if (assign->assignment_op != TokenType::Equal) {
                    const auto op = compound_assignment_binary_op(assign->assignment_op);
                    if (!op.has_value()) {
                        emit(BytecodeOp::Fail, assign->span, 0, "Unsupported compound member assignment operator.");
                        return;
                    }
                    const std::string rhs_name = make_temp_name("__zephyr_assign_rhs_");
                    const std::string object_name = make_temp_name("__zephyr_assign_object_");
                    enter_local_scope();
                    emit(BytecodeOp::EnterScope, assign->span);
                    compile_expr(assign->value.get());
                    emit_define_symbol(rhs_name, assign->span);
                    compile_expr(member->object.get());
                    emit_define_symbol(object_name, member->span);
                    emit_load_symbol(object_name, member->span);
                    emit(BytecodeOp::LoadMember, member->span, 0, member->member);
                    emit_load_symbol(rhs_name, assign->span);
                    switch (*op) {
                        case TokenType::Plus: emit(BytecodeOp::Add, assign->span); break;
                        case TokenType::Minus: emit(BytecodeOp::Subtract, assign->span); break;
                        case TokenType::Star: emit(BytecodeOp::Multiply, assign->span); break;
                        case TokenType::Slash: emit(BytecodeOp::Divide, assign->span); break;
                        default:
                            emit(BytecodeOp::Fail, assign->span, 0, "Unsupported compound member assignment operator.");
                            emit(BytecodeOp::ExitScope, assign->span);
                            exit_local_scope();
                            return;
                    }
                    emit_load_symbol(object_name, member->span);
                    emit(BytecodeOp::StoreMember, member->span, 0, member->member);
                    emit(BytecodeOp::ExitScope, assign->span);
                    exit_local_scope();
                    return;
                }
                compile_expr(assign->value.get());
                compile_expr(member->object.get());
                emit(BytecodeOp::StoreMember, member->span, 0, member->member);
                return;
            }
            if (auto* index = dynamic_cast<IndexExpr*>(assign->target.get())) {
                if (assign->assignment_op != TokenType::Equal) {
                    const auto op = compound_assignment_binary_op(assign->assignment_op);
                    if (!op.has_value()) {
                        emit(BytecodeOp::Fail, assign->span, 0, "Unsupported compound index assignment operator.");
                        return;
                    }
                    const std::string rhs_name = make_temp_name("__zephyr_assign_rhs_");
                    const std::string object_name = make_temp_name("__zephyr_assign_object_");
                    const std::string index_name = make_temp_name("__zephyr_assign_index_");
                    enter_local_scope();
                    emit(BytecodeOp::EnterScope, assign->span);
                    compile_expr(assign->value.get());
                    emit_define_symbol(rhs_name, assign->span);
                    compile_expr(index->object.get());
                    emit_define_symbol(object_name, index->object->span);
                    compile_expr(index->index.get());
                    emit_define_symbol(index_name, index->index->span);
                    emit_load_symbol(object_name, index->object->span);
                    emit_load_symbol(index_name, index->index->span);
                    emit(BytecodeOp::LoadIndex, index->span);
                    emit_load_symbol(rhs_name, assign->span);
                    switch (*op) {
                        case TokenType::Plus: emit(BytecodeOp::Add, assign->span); break;
                        case TokenType::Minus: emit(BytecodeOp::Subtract, assign->span); break;
                        case TokenType::Star: emit(BytecodeOp::Multiply, assign->span); break;
                        case TokenType::Slash: emit(BytecodeOp::Divide, assign->span); break;
                        default:
                            emit(BytecodeOp::Fail, assign->span, 0, "Unsupported compound index assignment operator.");
                            emit(BytecodeOp::ExitScope, assign->span);
                            exit_local_scope();
                            return;
                    }
                    emit_load_symbol(object_name, index->object->span);
                    emit_load_symbol(index_name, index->index->span);
                    emit(BytecodeOp::StoreIndex, index->span);
                    emit(BytecodeOp::ExitScope, assign->span);
                    exit_local_scope();
                    return;
                }
                compile_expr(assign->value.get());
                compile_expr(index->object.get());
                compile_expr(index->index.get());
                emit(BytecodeOp::StoreIndex, index->span);
                return;
            }
            emit(BytecodeOp::Fail, assign->span, 0, "Unsupported assignment target.");
            return;
        }
        if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
            compile_expr(member->object.get());
            emit(BytecodeOp::LoadMember, member->span, 0, member->member);
            return;
        }
        if (auto* optional_member = dynamic_cast<OptionalMemberExpr*>(expr)) {
            compile_expr(optional_member->object.get());
            const int end_jump = emit_jump(BytecodeOp::JumpIfNilKeep, optional_member->span);
            emit(BytecodeOp::LoadMember, optional_member->span, 0, optional_member->member);
            patch_jump(end_jump);
            return;
        }
        if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
            compile_expr(index->object.get());
            compile_expr(index->index.get());
            emit(BytecodeOp::LoadIndex, index->span);
            return;
        }
        if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                compile_expr(member->object.get());
                for (auto& argument : call->arguments) {
                    compile_expr(argument.get());
                }
                emit(BytecodeOp::CallMember, call->span, static_cast<int>(call->arguments.size()), member->member);
                return;
            }
            compile_expr(call->callee.get());
            for (auto& argument : call->arguments) {
                compile_expr(argument.get());
            }
            emit(BytecodeOp::Call, call->span, static_cast<int>(call->arguments.size()));
            return;
        }
        if (auto* optional_call = dynamic_cast<OptionalCallExpr*>(expr)) {
            compile_expr(optional_call->object.get());
            const int end_jump = emit_jump(BytecodeOp::JumpIfNilKeep, optional_call->span);
            for (auto& argument : optional_call->arguments) {
                compile_expr(argument.get());
            }
            emit(BytecodeOp::CallMember, optional_call->span, static_cast<int>(optional_call->arguments.size()), optional_call->member);
            patch_jump(end_jump);
            return;
        }
        if (auto* coroutine = dynamic_cast<CoroutineExpr*>(expr)) {
            emit(BytecodeOp::MakeCoroutine,
                 coroutine->span,
                 0,
                 "<coroutine>",
                 coroutine->return_type.has_value() ? std::optional<std::string>(coroutine->return_type->display_name())
                                                    : std::nullopt,
                 false,
                 coroutine,
                 nullptr,
                 encode_param_metadata(coroutine->params),
                 nullptr,
                 compile_nested_function("<coroutine>", coroutine->params, coroutine->body.get(), true));
            return;
        }
        if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
            compile_expr(resume->target.get());
            emit(BytecodeOp::Resume, resume->span);
            return;
        }
        if (auto* struct_init = dynamic_cast<StructInitExpr*>(expr)) {
            std::vector<std::string> field_names;
            field_names.reserve(struct_init->fields.size());
            for (auto& field : struct_init->fields) {
                compile_expr(field.value.get());
                field_names.push_back(field.name);
            }
            emit(BytecodeOp::BuildStruct,
                 struct_init->span,
                 static_cast<int>(struct_init->fields.size()),
                 struct_init->type_name.display_name(),
                 std::nullopt,
                 false,
                 nullptr,
                 nullptr,
                 std::move(field_names));
            return;
        }
        if (auto* enum_init = dynamic_cast<EnumInitExpr*>(expr)) {
            for (auto& argument : enum_init->arguments) {
                compile_expr(argument.get());
            }
            emit(BytecodeOp::BuildEnum,
                 enum_init->span,
                 static_cast<int>(enum_init->arguments.size()),
                 enum_init->enum_name.display_name(),
                 enum_init->variant_name);
            return;
        }
        if (auto* match_expr = dynamic_cast<MatchExpr*>(expr)) {
            const std::string subject_name = make_temp_name("__zephyr_match_subject_");
            std::vector<int> end_jumps;

            enter_local_scope();
            emit(BytecodeOp::EnterScope, match_expr->span);
            compile_expr(match_expr->subject.get());
            emit_define_symbol(subject_name, match_expr->span);

            for (auto& arm : match_expr->arms) {
                enter_local_scope();
                emit(BytecodeOp::EnterScope, arm.pattern->span);
                std::vector<int> failure_jumps;
                std::vector<std::string> binding_names;
                std::vector<std::int32_t> binding_slots;
                collect_pattern_binding_names(arm.pattern.get(), binding_names);
                if (use_local_slots_) {
                    binding_slots.reserve(binding_names.size());
                    for (const auto& binding_name : binding_names) {
                        binding_slots.push_back(define_local_slot(binding_name));
                    }
                }
                emit_load_symbol(subject_name, arm.pattern->span);
                emit(BytecodeOp::BindPattern,
                     arm.pattern->span,
                     0,
                     {},
                     std::nullopt,
                     false,
                     nullptr,
                     nullptr,
                     std::move(binding_names),
                     arm.pattern.get());
                function_->metadata.back().jump_table = std::move(binding_slots);
                failure_jumps.push_back(emit_jump(BytecodeOp::JumpIfFalsePop, arm.pattern->span));
                if (arm.guard_expr) {
                    compile_expr(arm.guard_expr.get());
                    failure_jumps.push_back(emit_jump(BytecodeOp::JumpIfFalsePop, arm.pattern->span));
                }
                compile_expr(arm.expression.get());
                exit_local_scope();
                emit(BytecodeOp::ExitScope, arm.pattern->span);
                end_jumps.push_back(emit_jump(BytecodeOp::Jump, arm.pattern->span));

                const int failure_target = static_cast<int>(function_->instructions.size());
                for (int jump_index : failure_jumps) {
                    patch_jump_to(jump_index, failure_target);
                }
                emit(BytecodeOp::ExitScope, arm.pattern->span);
            }

            emit(BytecodeOp::MatchFail, match_expr->span, 0, "Match expression is not exhaustive.");
            const int outer_exit = static_cast<int>(function_->instructions.size());
            emit(BytecodeOp::ExitScope, match_expr->span);
            exit_local_scope();
            for (int jump_index : end_jumps) {
                patch_jump_to(jump_index, outer_exit);
            }
            return;
        }
        emit(BytecodeOp::Fail, expr->span, 0, "Unsupported expression node in bytecode compiler.");
    }

    std::shared_ptr<BytecodeFunction> function_;
    int temp_counter_ = 0;
    int next_slot_ = 0;
    std::unordered_map<std::string, std::uint8_t> cached_literal_regs_;
    std::unordered_map<std::string, TypeRef> type_bindings_;  // Generic type parameter bindings: T -> Int, U -> String, etc.
    bool disable_register_mode_ = false;
    bool use_local_slots_ = true;
    bool allow_return_ = true;
    bool has_env_ops_ = false;
    bool register_compile_failed_ = false;
    RegisterAllocator register_allocator_;
    std::vector<std::unordered_map<std::string, int>> local_scopes_;
    std::vector<LoopContext> loop_stack_;
    BytecodeCompiler* parent_ = nullptr;
};
