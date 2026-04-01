#include "zephyr_vm_dispatch.h"
#include <string.h>

/* ── Operand unpacking (mirrors C++ inline functions) ────────────────── */
static inline uint8_t unpack_dst(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline int unpack_idx(int32_t op) { return (int)((uint32_t)op >> 8); }
static inline uint8_t unpack_src(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline int unpack_jump_target(int32_t op) { return (int)((uint32_t)op >> 8); }
static inline uint8_t unpack_addi_dst(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline uint8_t unpack_addi_src(int32_t op) { return (uint8_t)((op >> 8) & 0xFF); }
static inline int64_t unpack_addi_imm(int32_t op) { return (int64_t)(int8_t)((op >> 16) & 0xFF); }
static inline uint8_t unpack_load_int_dst(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline int64_t unpack_load_int_val(int32_t op) { return (int64_t)((int32_t)op >> 8); }

/* R_SI_CMP_JUMP_FALSE unpacking */
static inline uint8_t unpack_cmp_s1(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline uint8_t unpack_cmp_s2(int32_t op) { return (uint8_t)((op >> 8) & 0xFF); }
static inline int unpack_cmp_op(int32_t op) { return (int)((uint32_t)op >> 16) & 0xFF; }

/* R_SI_ADDI_CMPI_LT_JUMP unpacking */
static inline uint8_t unpack_acj_reg(int32_t op) { return (uint8_t)(op & 0xFF); }
static inline int64_t unpack_acj_addi(int32_t op) { return (int64_t)(int8_t)((op >> 8) & 0xFF); }
static inline int64_t unpack_acj_limit(int32_t op) { return (int64_t)(int16_t)(op >> 16); }

/* ── Overflow-checked int48 arithmetic ───────────────────────────────── */
static inline int try_add_i48(int64_t a, int64_t b, int64_t* out) {
    if ((b > 0 && a > ZV_INT_MAX - b) || (b < 0 && a < ZV_INT_MIN - b)) return 0;
    *out = a + b; return 1;
}
static inline int try_sub_i48(int64_t a, int64_t b, int64_t* out) {
    if ((b > 0 && a < ZV_INT_MIN + b) || (b < 0 && a > ZV_INT_MAX + b)) return 0;
    *out = a - b; return 1;
}
static inline int try_mul_i48(int64_t a, int64_t b, int64_t* out) {
    if (a == 0 || b == 0) { *out = 0; return 1; }
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r)) return 0;
    if (r < ZV_INT_MIN || r > ZV_INT_MAX) return 0;
    *out = r; return 1;
}

/* ── Ensure register pool capacity ───────────────────────────────────── */
/* NOTE: Cannot realloc from C — pool is owned by C++ std::vector.
   If pool is too small, return ZVM_ERROR and let C++ handle resize + retry. */

int zephyr_vm_dispatch(ZDispatchState* s, const ZCallbacks* cb) {
    /* Local aliases for hot state */
    ZephyrVal* regs = s->regs;
    const ZInstruction* insns = s->instructions;
    size_t ip = s->ip;

#define DISPATCH() do { \
    const void* _target = dispatch_table[insns[ip].op]; \
    if (_target) goto *_target; \
    goto slow_path; \
} while(0)

#define NEXT() do { ++ip; DISPATCH(); } while(0)

    static const void* dispatch_table[ZOP_MAX] = {
        [ZOP_R_LOAD_CONST]            = &&op_R_LOAD_CONST,
        [ZOP_R_LOAD_INT]              = &&op_R_LOAD_INT,
        [ZOP_R_ADDI]                  = &&op_R_ADDI,
        [ZOP_R_ADD]                   = &&op_R_ADD,
        [ZOP_R_SUB]                   = &&op_R_SUB,
        [ZOP_R_MUL]                   = &&op_R_MUL,
        [ZOP_R_RETURN]                = &&op_R_RETURN,
        [ZOP_R_JUMP]                  = &&op_R_JUMP,
        [ZOP_R_JUMP_IF_FALSE]         = &&op_R_JUMP_IF_FALSE,
        [ZOP_R_JUMP_IF_TRUE]          = &&op_R_JUMP_IF_TRUE,
        [ZOP_R_CALL]                  = &&op_R_CALL,
        [ZOP_R_LOAD_GLOBAL]           = &&op_R_LOAD_GLOBAL,
        [ZOP_R_MOVE]                  = &&op_R_MOVE,
        [ZOP_R_NOT]                   = &&op_R_NOT,
        [ZOP_R_SI_CMP_JUMP_FALSE]     = &&op_R_SI_CMP_JUMP_FALSE,
        [ZOP_R_SI_ADDI_CMPI_LT_JUMP] = &&op_R_SI_ADDI_CMPI_LT_JUMP,
        /* All other opcodes: NULL entries fall through to slow_path */
    };

    DISPATCH();

    /* ── Hot opcode handlers ──────────────────────────────────────── */

op_R_LOAD_CONST: {
    const ZInstruction* i = &insns[ip];
    int idx = unpack_idx(i->operand);
    uint8_t dst = unpack_dst(i->operand);
    if (idx >= 0 && (size_t)idx < s->constants_count && s->int_constant_valid[idx]) {
        regs[dst] = zv_integer(s->int_constants[idx]);
        NEXT();
    }
    /* Non-int constant: fall back to C++ */
    goto slow_path;
}

op_R_LOAD_INT: {
    const ZInstruction* i = &insns[ip];
    regs[unpack_load_int_dst(i->operand)] = zv_integer(unpack_load_int_val(i->operand));
    NEXT();
}

op_R_ADDI: {
    const ZInstruction* i = &insns[ip];
    ZephyrVal src_val = regs[unpack_addi_src(i->operand)];
    int64_t imm = unpack_addi_imm(i->operand);
    if (zv_is_int(src_val)) {
        int64_t sum = zv_as_int(src_val) + imm;
        if (sum >= ZV_INT_MIN && sum <= ZV_INT_MAX) {
            regs[unpack_addi_dst(i->operand)] = zv_integer(sum);
            NEXT();
        }
    }
    goto slow_path;
}

op_R_ADD: {
    const ZInstruction* i = &insns[ip];
    ZephyrVal lhs = regs[i->src1], rhs = regs[i->src2];
    if (zv_is_int(lhs) && zv_is_int(rhs)) {
        int64_t r;
        if (try_add_i48(zv_as_int(lhs), zv_as_int(rhs), &r)) {
            regs[i->dst] = zv_integer(r);
            NEXT();
        }
    }
    goto slow_path;
}

op_R_SUB: {
    const ZInstruction* i = &insns[ip];
    ZephyrVal lhs = regs[i->src1], rhs = regs[i->src2];
    if (zv_is_int(lhs) && zv_is_int(rhs)) {
        int64_t r;
        if (try_sub_i48(zv_as_int(lhs), zv_as_int(rhs), &r)) {
            regs[i->dst] = zv_integer(r);
            NEXT();
        }
    }
    goto slow_path;
}

op_R_MUL: {
    const ZInstruction* i = &insns[ip];
    ZephyrVal lhs = regs[i->src1], rhs = regs[i->src2];
    if (zv_is_int(lhs) && zv_is_int(rhs)) {
        int64_t r;
        if (try_mul_i48(zv_as_int(lhs), zv_as_int(rhs), &r)) {
            regs[i->dst] = zv_integer(r);
            NEXT();
        }
    }
    goto slow_path;
}

op_R_MOVE: {
    const ZInstruction* i = &insns[ip];
    regs[i->dst] = regs[i->src1];
    NEXT();
}

op_R_NOT: {
    const ZInstruction* i = &insns[ip];
    regs[i->dst] = zv_boolean(!zv_is_truthy(regs[i->src1]));
    NEXT();
}

op_R_JUMP: {
    ip = (size_t)insns[ip].operand;
    DISPATCH();
}

op_R_JUMP_IF_FALSE: {
    const ZInstruction* i = &insns[ip];
    if (!zv_is_truthy(regs[unpack_src(i->operand)])) {
        ip = (size_t)unpack_jump_target(i->operand);
    } else {
        ++ip;
    }
    DISPATCH();
}

op_R_JUMP_IF_TRUE: {
    const ZInstruction* i = &insns[ip];
    if (zv_is_truthy(regs[unpack_src(i->operand)])) {
        ip = (size_t)unpack_jump_target(i->operand);
    } else {
        ++ip;
    }
    DISPATCH();
}

op_R_RETURN: {
    ZephyrVal rv = regs[insns[ip].src1];
    if (s->call_stack_sp > 0) {
        ZCallFrame* parent = &s->call_stack[s->call_stack_sp - 1];
        /* Restore from frame */
        s->register_sp = parent->reg_base;
        ip = parent->ip;
        s->active_reg_count = parent->reg_count;
        regs = s->reg_pool + parent->reg_base;
        regs[parent->dst] = rv;
        --s->call_stack_sp;
        /* If different function, need C++ to restore chunk pointers */
        if (!parent->same_func) {
            s->regs = regs;
            s->ip = ip;
            s->return_value = rv;
            return ZVM_SLOW_OPCODE; /* C++ will restore chunk and re-enter */
        }
        DISPATCH();
    }
    s->return_value = rv;
    s->ip = ip;
    s->regs = regs;
    return ZVM_RETURN;
}

op_R_CALL: {
    /* Save ip and fall back — C++ handles the call dispatch */
    s->ip = ip;
    s->regs = regs;
    return ZVM_SLOW_OPCODE;
}

op_R_LOAD_GLOBAL: {
    const ZInstruction* i = &insns[ip];
    uint8_t dst = unpack_dst(i->operand);
    int slot = unpack_idx(i->operand);
    if (s->globals.resolved && slot >= 0 && (size_t)slot < s->globals.count) {
        /* Direct flat cache read — the binding pointer gives us the Value */
        s->ip = ip;
        s->regs = regs;
        regs[dst] = cb->read_global(cb->runtime, s, slot);
        NEXT();
    }
    goto slow_path;
}

op_R_SI_CMP_JUMP_FALSE: {
    const ZInstruction* i = &insns[ip];
    uint8_t s1 = unpack_cmp_s1(i->operand);
    uint8_t s2 = unpack_cmp_s2(i->operand);
    int cmp_op = unpack_cmp_op(i->operand);
    ZephyrVal lhs = regs[s1], rhs = regs[s2];
    if (zv_is_int(lhs) && zv_is_int(rhs)) {
        int64_t a = zv_as_int(lhs), b = zv_as_int(rhs);
        int cmp_val = 0;
        switch (cmp_op) {
            case 0: cmp_val = a < b;  break;  /* Less */
            case 1: cmp_val = a <= b; break;  /* LessEqual */
            case 2: cmp_val = a > b;  break;  /* Greater */
            case 3: cmp_val = a >= b; break;  /* GreaterEqual */
            case 4: cmp_val = a == b; break;  /* Equal */
            case 5: cmp_val = a != b; break;  /* NotEqual */
            default: cmp_val = 0; break;
        }
        if (cmp_val) {
            /* Condition TRUE: skip jump, continue to next instruction */
            ++ip;
            DISPATCH();
        }
        /* Condition FALSE: fall back to C++ for metadata jump target lookup */
        s->ip = ip;
        s->regs = regs;
        return ZVM_SLOW_OPCODE;
    }
    goto slow_path;
}

op_R_SI_ADDI_CMPI_LT_JUMP: {
    const ZInstruction* i = &insns[ip];
    uint8_t reg = unpack_acj_reg(i->operand);
    int64_t addi = unpack_acj_addi(i->operand);
    int64_t limit = unpack_acj_limit(i->operand);
    ZephyrVal v = regs[reg];
    if (zv_is_int(v)) {
        int64_t val = zv_as_int(v) + addi;
        if (val >= ZV_INT_MIN && val <= ZV_INT_MAX) {
            regs[reg] = zv_integer(val);
            if (val < limit) {
                ip = (size_t)i->ic_slot; /* body_start stored in ic_slot */
            } else {
                ++ip;
            }
            DISPATCH();
        }
    }
    goto slow_path;
}

    /* ── Slow path: fall back to C++ ──────────────────────────────── */
slow_path:
    s->ip = ip;
    s->regs = regs;
    {
        int rc = cb->slow_opcode(s, cb->runtime, ip);
        if (rc != ZVM_OK) return rc;
        /* C++ advanced ip and may have changed regs */
        ip = s->ip;
        regs = s->regs;
        insns = s->instructions; /* may have changed after frame switch */
        DISPATCH();
    }

#undef DISPATCH
#undef NEXT
}
