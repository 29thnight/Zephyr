#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ── NaN-boxed Value (matches C++ zephyr::Value exactly) ─────────────── */
typedef uint64_t ZephyrVal;

#define ZV_TAG_MASK     0xFFFF000000000000ull
#define ZV_PAYLOAD_MASK 0x0000FFFFFFFFFFFFull
#define ZV_NIL_TAG      0xFFFA000000000000ull
#define ZV_BOOL_TAG     0xFFFB000000000000ull
#define ZV_PTR_TAG      0xFFFC000000000000ull
#define ZV_INT_TAG      0xFFFE000000000000ull
#define ZV_INT_MIN      (-(int64_t)(1ll << 47))
#define ZV_INT_MAX      ((int64_t)((1ll << 47) - 1))

static inline int zv_is_int(ZephyrVal v) { return (v & ZV_TAG_MASK) == ZV_INT_TAG; }
static inline int zv_is_object(ZephyrVal v) { return (v & ZV_TAG_MASK) == ZV_PTR_TAG && (v & ZV_PAYLOAD_MASK) != 0; }
static inline int zv_is_truthy(ZephyrVal v) {
    if (v == ZV_NIL_TAG) return 0;
    if ((v & ZV_TAG_MASK) == ZV_BOOL_TAG) return (int)(v & 1ull);
    return 1;
}
static inline int64_t zv_as_int(ZephyrVal v) {
    uint64_t payload = v & ZV_PAYLOAD_MASK;
    if (payload & (1ull << 47)) payload |= ~ZV_PAYLOAD_MASK;
    return (int64_t)payload;
}
static inline ZephyrVal zv_integer(int64_t n) {
    return ZV_INT_TAG | ((uint64_t)n & ZV_PAYLOAD_MASK);
}
static inline ZephyrVal zv_boolean(int b) {
    return ZV_BOOL_TAG | (b ? 1ull : 0ull);
}
static inline ZephyrVal zv_nil(void) { return ZV_NIL_TAG; }

/* ── Opcode constants (must match C++ BytecodeOp enum exactly) ───────── */
/* These values are from the BytecodeOp enum in zephyr_compiler.hpp.
   Count from 0: Nop=0, LoadConst=1, ... Return=75, R_ADD=76 etc. */
enum {
    ZOP_R_ADD = 70, ZOP_R_SUB = 71, ZOP_R_MUL = 72, ZOP_R_DIV = 73, ZOP_R_MOD = 74,
    ZOP_R_LOAD_CONST = 75, ZOP_R_LOAD_INT = 76, ZOP_R_LOAD_GLOBAL = 77,
    ZOP_R_STORE_GLOBAL = 78, ZOP_R_MOVE = 79, ZOP_R_CALL = 80, ZOP_R_RETURN = 81,
    ZOP_R_JUMP = 82, ZOP_R_JUMP_IF_FALSE = 83, ZOP_R_JUMP_IF_TRUE = 84,
    ZOP_R_LT = 85, ZOP_R_LE = 86, ZOP_R_GT = 87, ZOP_R_GE = 88,
    ZOP_R_EQ = 89, ZOP_R_NE = 90, ZOP_R_NOT = 91, ZOP_R_NEG = 92,
    ZOP_R_YIELD = 93,
    ZOP_R_SI_ADD_STORE = 94, ZOP_R_SI_SUB_STORE = 95, ZOP_R_SI_MUL_STORE = 96,
    ZOP_R_SI_CMP_JUMP_FALSE = 97, ZOP_R_SI_CMPI_JUMP_FALSE = 98,
    ZOP_R_SI_LOAD_ADD_STORE = 99, ZOP_R_SI_MODI_ADD_STORE = 100,
    ZOP_R_SI_ADDI_CMPI_LT_JUMP = 101, ZOP_R_SI_LOOP_STEP = 102,
    ZOP_R_SPILL_LOAD = 103, ZOP_R_SPILL_STORE = 104,
    ZOP_R_ADDI = 105, ZOP_R_MODI = 106, ZOP_R_ADDI_JUMP = 107,
    ZOP_R_LOAD_MEMBER = 108, ZOP_R_STORE_MEMBER = 109,
    ZOP_R_CALL_MEMBER = 110, ZOP_R_BUILD_STRUCT = 111,
    ZOP_R_BUILD_ARRAY = 112, ZOP_R_LOAD_INDEX = 113,
    ZOP_R_LOAD_UPVALUE = 114, ZOP_R_STORE_UPVALUE = 115,
    ZOP_R_MAKE_FUNCTION = 116,
    ZOP_R_MAKE_COROUTINE = 117,
    ZOP_R_RESUME = 118,
    ZOP_R_SI_MUL_ADD = 119,
    ZOP_MAX = 128
};

/* ── Hot instruction layout (matches C++ HotInstruction, 8 bytes) ────── */
typedef struct {
    int32_t op;
    union {
        int32_t operand;
        struct { uint8_t dst, src1, src2, operand_a; };
    };
} ZHotInstruction;

/* ── Compact instruction layout (matches C++ CompactInstruction) ─────── */
typedef struct {
    int32_t op;
    union {
        int32_t operand;
        struct { uint8_t dst, src1, src2, operand_a; };
    };
    uint32_t span_line;
    uint32_t ic_slot;
    void*    ic_shape;
    int32_t  jump_target;
} ZInstruction;

/* ── Iterative call frame ────────────────────────────────────────────── */
typedef struct {
    size_t ip;
    size_t reg_base;
    size_t reg_count;
    size_t dst;
    int same_func; /* 1 = same function recursion, skip chunk restore */
} ZCallFrame;

/* ── Resolved global binding (opaque pointer to C++ Binding) ─────────── */
typedef struct {
    void** bindings;    /* Binding* array */
    int resolved;       /* 1 = already resolved */
    size_t count;
} ZGlobalCache;

/* ── Dispatch state ──────────────────────────────────────────────────── */
typedef struct {
    ZephyrVal* regs;
    ZephyrVal* reg_pool;
    size_t reg_pool_capacity;
    size_t register_sp;
    size_t ip;
    const ZHotInstruction* hot_instructions;  /* hot array (op + operand, 8B each) */
    const ZInstruction* instructions;         /* cold array (IC/jump_target/span) */
    size_t instructions_size;

    /* Bytecode constants: array of int64_t for fast int access */
    const int64_t* int_constants;   /* pre-extracted int constants, NULL if non-int */
    const int* int_constant_valid;  /* 1 = int_constants[i] is valid */
    size_t constants_count;

    /* Global binding cache (from BytecodeFunction) */
    ZGlobalCache globals;

    /* Iterative call stack */
    ZCallFrame* call_stack;
    size_t call_stack_sp;
    size_t call_stack_capacity;

    /* For R_CALL: function info to check */
    const void* active_chunk; /* BytecodeFunction* */
    size_t active_reg_count;

    /* Upvalue view: array of pointers to UpvalueCellObject::value fields */
    ZephyrVal** upvalue_cells;
    size_t upvalue_count;

    /* Coroutine fast-path state */
    ZephyrVal coroutine_value; /* resume arg or yield value */

    /* Return value */
    ZephyrVal return_value;

    /* Deopt reason tracking */
    int deopt_reason;

    /* Error state */
    int error;
    size_t error_ip;
} ZDispatchState;

/* ── Result codes ────────────────────────────────────────────────────── */
#define ZVM_OK          0
#define ZVM_RETURN      1
#define ZVM_ERROR       2
#define ZVM_SLOW_OPCODE 3  /* fallback to C++ for this opcode */
#define ZVM_COROUTINE_RESUME 4  /* R_RESUME: C++ handles frame swap */
#define ZVM_COROUTINE_YIELD  5  /* R_YIELD: C++ handles frame restore */

/* ── Deopt reason codes ─────────────────────────────────────────────── */
enum {
    ZDEOPT_NONE = 0,
    ZDEOPT_NON_INT_ARITH,       /* non-integer operand in arithmetic */
    ZDEOPT_OVERFLOW,             /* integer overflow */
    ZDEOPT_NON_INT_CONST,        /* non-integer constant load */
    ZDEOPT_GLOBAL_UNRESOLVED,    /* global not in flat cache */
    ZDEOPT_UPVALUE_BOUNDS,       /* upvalue slot out of range */
    ZDEOPT_COLD_OPCODE,          /* opcode not in C dispatch table */
    ZDEOPT_CALL_DISPATCH,        /* non-same-function call */
    ZDEOPT_FRAME_RESTORE,        /* cross-function return */
    ZDEOPT_CMP_JUMP_FALSE,       /* comparison false branch needs metadata */
    ZDEOPT_COROUTINE_RESUME,     /* R_RESUME needs C++ frame swap */
    ZDEOPT_COROUTINE_YIELD,      /* R_YIELD needs C++ frame restore */
};

/* ── Callback for slow opcodes ───────────────────────────────────────── */
typedef int (*ZSlowOpcodeHandler)(ZDispatchState* state, void* runtime, size_t opcode_ip);
/* Called for slow path: non-int arithmetic fallback, member access, etc.
   Should execute the opcode at state->ip and advance ip.
   Returns ZVM_OK to continue, ZVM_RETURN/ZVM_ERROR to stop. */

typedef int (*ZCallHandler)(ZDispatchState* state, void* runtime,
                            ZephyrVal callee, ZephyrVal* args, int argc,
                            size_t dst_reg);
/* Called for non-same-function R_CALL that needs C++ call_value. */

typedef ZephyrVal (*ZReadGlobalHandler)(void* runtime, ZDispatchState* state, int slot);
/* Read a global variable by slot index. Returns the value. */

typedef struct {
    ZSlowOpcodeHandler slow_opcode;
    ZCallHandler       call_handler;
    ZReadGlobalHandler read_global;
    void* runtime; /* opaque Runtime* */
} ZCallbacks;

/* ── Main dispatch function ──────────────────────────────────────────── */
int zephyr_vm_dispatch(ZDispatchState* state, const ZCallbacks* cb);

#ifdef __cplusplus
}
#endif
