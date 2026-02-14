/**
 * @file recompiler.c
 * @brief R5900 instruction-to-C translator — pure C port.
 *
 * Migrated from ps2recomp's code_generator.cpp (C++).
 * This file replaces the old tools/convert.c (x86 → C converter).
 * Now translates decoded R5900 MIPS instructions to C source code.
 *
 * Each recomp_translate_*() function writes a C statement into a buffer
 * using snprintf (replacing C++ fmt::format).  SSE intrinsics from the
 * original are preserved as macro references (PS2_PADDW, PS2_VADD, etc.)
 * so the generated code can be compiled with the ps2_runtime_macros.h header.
 *
 * References:
 *   - ps2recomp  ps2xRecomp/src/code_generator.cpp  (original C++ source)
 *   - "See MIPS Run" (Dominic Sweetman) — instruction semantics
 *   - ps2tek  https://psi-rockin.github.io/ps2tek/  — EE instruction tables
 *
 * Original convert.c (x86 → pseudo-C) kept at bottom for study reference.
 */

#include "../include/recompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Main dispatcher — translates any decoded R5900 instruction
 * ========================================================================= */

int recomp_translate(const r5900_inst_t *inst, char *buf, size_t sz)
{
    if (!inst || !buf || sz == 0) return -1;

    /* MMI dispatched first (like the C++ version) */
    if (inst->flags.is_mmi)
        return recomp_translate_mmi(inst, buf, sz);

    switch (inst->opcode) {
    case OP_SPECIAL: return recomp_translate_special(inst, buf, sz);
    case OP_REGIMM:  return recomp_translate_regimm(inst, buf, sz);
    case OP_COP0:    return recomp_translate_cop0(inst, buf, sz);
    case OP_COP1:    return recomp_translate_fpu(inst, buf, sz);
    case OP_COP2:    return recomp_translate_vu(inst, buf, sz);

    /* --- I-type ALU --- */
    case OP_ADDI:
        if (inst->rt == 0) return snprintf(buf, sz, "// NOP (addi to $zero)");
        return snprintf(buf, sz,
            "{ uint32_t tmp; bool ov; "
            "ADD32_OV(GPR_U32(ctx, %u), (int32_t)%d, tmp, ov); "
            "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
            "else SET_GPR_S32(ctx, %u, (int32_t)tmp); }",
            inst->rs, inst->simmediate, inst->rt);

    case OP_ADDIU:
        if (inst->rt == 0) return snprintf(buf, sz, "// NOP (addiu $zero, ...)");
        return snprintf(buf, sz, "SET_GPR_S32(ctx, %u, ADD32(GPR_U32(ctx, %u), %d));",
                        inst->rt, inst->rs, inst->simmediate);

    case OP_SLTI:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLT32(GPR_S32(ctx, %u), %d));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_SLTIU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLTU32(GPR_U32(ctx, %u), %u));",
                        inst->rt, inst->rs, inst->immediate);
    case OP_ANDI:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, AND32(GPR_U32(ctx, %u), %u));",
                        inst->rt, inst->rs, inst->immediate);
    case OP_ORI:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, OR32(GPR_U32(ctx, %u), %u));",
                        inst->rt, inst->rs, inst->immediate);
    case OP_XORI:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, XOR32(GPR_U32(ctx, %u), %u));",
                        inst->rt, inst->rs, inst->immediate);
    case OP_LUI:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ((uint32_t)%u << 16));",
                        inst->rt, inst->immediate);

    /* --- 64-bit immediate --- */
    case OP_DADDI:
        return snprintf(buf, sz,
            "{ int64_t src = (int64_t)GPR_S64(ctx, %u); "
            "int64_t imm = (int64_t)%d; "
            "int64_t res = src + imm; "
            "if (((src ^ imm) >= 0) && ((src ^ res) < 0)) "
            "    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
            "else SET_GPR_S64(ctx, %u, res); }",
            inst->rs, inst->simmediate, inst->rt);
    case OP_DADDIU:
        return snprintf(buf, sz,
            "SET_GPR_S64(ctx, %u, (int64_t)GPR_S64(ctx, %u) + (int64_t)%d);",
            inst->rt, inst->rs, inst->simmediate);

    /* --- Load --- */
    case OP_LB:
        return snprintf(buf, sz, "SET_GPR_S32(ctx, %u, (int8_t)READ8(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LH:
        return snprintf(buf, sz, "SET_GPR_S32(ctx, %u, (int16_t)READ16(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LW:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, READ32(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LBU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, (uint8_t)READ8(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LHU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, (uint16_t)READ16(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LWU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, READ32(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LD:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, READ64(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);
    case OP_LQ:
        return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, READ128(ADD32(GPR_U32(ctx, %u), %d)));",
                        inst->rt, inst->rs, inst->simmediate);

    /* --- Unaligned loads --- */
    case OP_LWL:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = ((~addr) & 3) << 3; "
            "uint32_t mask = 0xFFFFFFFF >> shift; "
            "uint32_t word = READ32(addr & ~3); "
            "SET_GPR_U32(ctx, %u, (GPR_U32(ctx,%u) & ~mask) | ((word >> shift) & mask)); }",
            inst->rs, inst->simmediate, inst->rt, inst->rt);
    case OP_LWR:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = (addr & 3) << 3; "
            "uint32_t mask = 0xFFFFFFFF << shift; "
            "uint32_t word = READ32(addr & ~3); "
            "SET_GPR_U32(ctx, %u, (GPR_U32(ctx,%u) & ~mask) | (word << shift)); }",
            inst->rs, inst->simmediate, inst->rt, inst->rt);
    case OP_LDL:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = (addr & 7) << 3; "
            "uint64_t mask = 0xFFFFFFFFFFFFFFFFULL << shift; "
            "uint64_t aligned_data = READ64(addr & ~7ULL); "
            "SET_GPR_U64(ctx, %u, (GPR_U64(ctx, %u) & ~mask) | (aligned_data & mask)); }",
            inst->rs, inst->simmediate, inst->rt, inst->rt);
    case OP_LDR:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = ((~addr) & 7) << 3; "
            "uint64_t mask = 0xFFFFFFFFFFFFFFFFULL >> shift; "
            "uint64_t aligned_data = READ64(addr & ~7ULL); "
            "SET_GPR_U64(ctx, %u, (GPR_U64(ctx, %u) & ~mask) | (aligned_data & mask)); }",
            inst->rs, inst->simmediate, inst->rt, inst->rt);

    /* --- Store --- */
    case OP_SB:
        return snprintf(buf, sz, "WRITE8(ADD32(GPR_U32(ctx, %u), %d), (uint8_t)GPR_U32(ctx, %u));",
                        inst->rs, inst->simmediate, inst->rt);
    case OP_SH:
        return snprintf(buf, sz, "WRITE16(ADD32(GPR_U32(ctx, %u), %d), (uint16_t)GPR_U32(ctx, %u));",
                        inst->rs, inst->simmediate, inst->rt);
    case OP_SW:
        return snprintf(buf, sz, "WRITE32(ADD32(GPR_U32(ctx, %u), %d), GPR_U32(ctx, %u));",
                        inst->rs, inst->simmediate, inst->rt);
    case OP_SD:
        return snprintf(buf, sz, "WRITE64(ADD32(GPR_U32(ctx, %u), %d), GPR_U64(ctx, %u));",
                        inst->rs, inst->simmediate, inst->rt);
    case OP_SQ:
        return snprintf(buf, sz, "WRITE128(ADD32(GPR_U32(ctx, %u), %d), GPR_VEC(ctx, %u));",
                        inst->rs, inst->simmediate, inst->rt);

    /* --- Unaligned stores --- */
    case OP_SWL:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = (addr & 3) << 3; "
            "uint32_t mask = 0xFFFFFFFF << shift; "
            "uint32_t aligned_addr = addr & ~3; "
            "uint32_t old_data = READ32(aligned_addr); "
            "uint32_t new_data = (old_data & ~mask) | (GPR_U32(ctx, %u) & mask); "
            "WRITE32(aligned_addr, new_data); }",
            inst->rs, inst->simmediate, inst->rt);
    case OP_SWR:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = ((~addr) & 3) << 3; "
            "uint32_t mask = 0xFFFFFFFF >> shift; "
            "uint32_t aligned_addr = addr & ~3; "
            "uint32_t old_data = READ32(aligned_addr); "
            "uint32_t new_data = (old_data & ~mask) | (GPR_U32(ctx, %u) & mask); "
            "WRITE32(aligned_addr, new_data); }",
            inst->rs, inst->simmediate, inst->rt);
    case OP_SDL:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = (addr & 7) << 3; "
            "uint64_t mask = 0xFFFFFFFFFFFFFFFFULL << shift; "
            "uint64_t aligned_addr = addr & ~7ULL; "
            "uint64_t old_data = READ64(aligned_addr); "
            "uint64_t new_data = (old_data & ~mask) | (GPR_U64(ctx, %u) & mask); "
            "WRITE64(aligned_addr, new_data); }",
            inst->rs, inst->simmediate, inst->rt);
    case OP_SDR:
        return snprintf(buf, sz,
            "{ uint32_t addr = ADD32(GPR_U32(ctx, %u), %d); "
            "uint32_t shift = ((~addr) & 7) << 3; "
            "uint64_t mask = 0xFFFFFFFFFFFFFFFFULL >> shift; "
            "uint64_t aligned_addr = addr & ~7ULL; "
            "uint64_t old_data = READ64(aligned_addr); "
            "uint64_t new_data = (old_data & ~mask) | (GPR_U64(ctx, %u) & mask); "
            "WRITE64(aligned_addr, new_data); }",
            inst->rs, inst->simmediate, inst->rt);

    /* --- FPU load/store --- */
    case OP_LWC1:
        return snprintf(buf, sz,
            "{ uint32_t val = READ32(ADD32(GPR_U32(ctx, %u), %d)); ctx->f[%u] = *(float*)&val; }",
            inst->rs, inst->simmediate, inst->rt);
    case OP_SWC1:
        return snprintf(buf, sz,
            "{ float val = ctx->f[%u]; WRITE32(ADD32(GPR_U32(ctx, %u), %d), *(uint32_t*)&val); }",
            inst->rt, inst->rs, inst->simmediate);

    /* --- VU load/store (LQC2/SQC2 encoded as LDC2/SDC2) --- */
    case OP_LDC2:
        return snprintf(buf, sz,
            "ctx->vu0_vf[%u] = VEC_CAST_I2F(READ128(ADD32(GPR_U32(ctx, %u), %d)));",
            inst->rt, inst->rs, inst->simmediate);
    case OP_SDC2:
        return snprintf(buf, sz,
            "WRITE128(ADD32(GPR_U32(ctx, %u), %d), VEC_CAST_F2I(ctx->vu0_vf[%u]));",
            inst->rs, inst->simmediate, inst->rt);

    /* --- Branches / Jumps — handled by branch logic --- */
    case OP_J:
    case OP_JAL:
        return snprintf(buf, sz, "// J/JAL 0x%X - Handled by branch logic",
                        (inst->address & 0xF0000000) | (inst->target << 2));
    case OP_BEQ: case OP_BNE: case OP_BLEZ: case OP_BGTZ:
    case OP_BEQL: case OP_BNEL: case OP_BLEZL: case OP_BGTZL:
        return snprintf(buf, sz, "// Branch at 0x%08X - Handled by branch logic",
                        inst->address);

    /* --- Cache / Prefetch --- */
    case OP_CACHE: return snprintf(buf, sz, "// CACHE instruction (ignored)");
    case OP_PREF:  return snprintf(buf, sz, "// PREF instruction (ignored)");

    default:
        return snprintf(buf, sz, "// Unhandled opcode: 0x%X", inst->opcode);
    }
}

/* =========================================================================
 * SPECIAL instructions
 * ========================================================================= */

int recomp_translate_special(const r5900_inst_t *inst, char *buf, size_t sz)
{
    switch (inst->function) {
    case SPEC_SLL:
        if (inst->rd == 0 && inst->rt == 0 && inst->sa == 0)
            return snprintf(buf, sz, "// NOP");
        if (inst->rd == 0) return snprintf(buf, sz, "%s", "");
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLL32(GPR_U32(ctx, %u), %u));",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_SRL:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SRL32(GPR_U32(ctx, %u), %u));",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_SRA:
        return snprintf(buf, sz, "SET_GPR_S32(ctx, %u, SRA32(GPR_S32(ctx, %u), %u));",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_SLLV:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLL32(GPR_U32(ctx, %u), GPR_U32(ctx, %u) & 0x1F));",
                        inst->rd, inst->rt, inst->rs);
    case SPEC_SRLV:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SRL32(GPR_U32(ctx, %u), GPR_U32(ctx, %u) & 0x1F));",
                        inst->rd, inst->rt, inst->rs);
    case SPEC_SRAV:
        return snprintf(buf, sz, "SET_GPR_S32(ctx, %u, SRA32(GPR_S32(ctx, %u), GPR_U32(ctx, %u) & 0x1F));",
                        inst->rd, inst->rt, inst->rs);

    case SPEC_JR:
        return snprintf(buf, sz, "// JR $%u - Handled by branch logic", inst->rs);
    case SPEC_JALR:
        return snprintf(buf, sz, "// JALR $%u, $%u - Handled by branch logic", inst->rd, inst->rs);

    case SPEC_SYSCALL:
        return snprintf(buf, sz, "runtime->handleSyscall(rdram, ctx);");
    case SPEC_BREAK:
        return snprintf(buf, sz, "runtime->handleBreak(rdram, ctx);");
    case SPEC_SYNC:
        return snprintf(buf, sz, "// SYNC instruction - memory barrier");

    case SPEC_MFHI: return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->hi);", inst->rd);
    case SPEC_MTHI: return snprintf(buf, sz, "ctx->hi = GPR_U32(ctx, %u);", inst->rs);
    case SPEC_MFLO: return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->lo);", inst->rd);
    case SPEC_MTLO: return snprintf(buf, sz, "ctx->lo = GPR_U32(ctx, %u);", inst->rs);

    case SPEC_MULT:
        return snprintf(buf, sz,
            "{ int64_t result = (int64_t)GPR_S32(ctx, %u) * (int64_t)GPR_S32(ctx, %u); "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            inst->rs, inst->rt);
    case SPEC_MULTU:
        return snprintf(buf, sz,
            "{ uint64_t result = (uint64_t)GPR_U32(ctx, %u) * (uint64_t)GPR_U32(ctx, %u); "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            inst->rs, inst->rt);
    case SPEC_DIV:
        return snprintf(buf, sz,
            "{ int32_t divisor = GPR_S32(ctx, %u); "
            "if (divisor != 0) { ctx->lo = (uint32_t)(GPR_S32(ctx, %u) / divisor); "
            "ctx->hi = (uint32_t)(GPR_S32(ctx, %u) %% divisor); } "
            "else { ctx->lo = (GPR_S32(ctx,%u) < 0) ? 1 : -1; ctx->hi = GPR_S32(ctx,%u); } }",
            inst->rt, inst->rs, inst->rs, inst->rs, inst->rs);
    case SPEC_DIVU:
        return snprintf(buf, sz,
            "{ uint32_t divisor = GPR_U32(ctx, %u); "
            "if (divisor != 0) { ctx->lo = GPR_U32(ctx, %u) / divisor; "
            "ctx->hi = GPR_U32(ctx, %u) %% divisor; } "
            "else { ctx->lo = 0xFFFFFFFF; ctx->hi = GPR_U32(ctx,%u); } }",
            inst->rt, inst->rs, inst->rs, inst->rs);

    case SPEC_ADD:
        return snprintf(buf, sz,
            "{ uint32_t tmp; bool ov; "
            "ADD32_OV(GPR_U32(ctx, %u), GPR_U32(ctx, %u), tmp, ov); "
            "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
            "else SET_GPR_S32(ctx, %u, (int32_t)tmp); }",
            inst->rs, inst->rt, inst->rd);
    case SPEC_ADDU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ADD32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_SUB:
        return snprintf(buf, sz,
            "{ uint32_t tmp; bool ov; "
            "SUB32_OV(GPR_U32(ctx, %u), GPR_U32(ctx, %u), tmp, ov); "
            "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
            "else SET_GPR_S32(ctx, %u, (int32_t)tmp); }",
            inst->rs, inst->rt, inst->rd);
    case SPEC_SUBU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SUB32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_AND:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, AND32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_OR:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, OR32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_XOR:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, XOR32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_NOR:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, NOR32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_SLT:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLT32(GPR_S32(ctx, %u), GPR_S32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_SLTU:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, SLTU32(GPR_U32(ctx, %u), GPR_U32(ctx, %u)));",
                        inst->rd, inst->rs, inst->rt);

    case SPEC_MOVZ:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) == 0) SET_GPR_U32(ctx, %u, GPR_U32(ctx, %u));",
                        inst->rt, inst->rd, inst->rs);
    case SPEC_MOVN:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) != 0) SET_GPR_U32(ctx, %u, GPR_U32(ctx, %u));",
                        inst->rt, inst->rd, inst->rs);

    case SPEC_MFSA:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->sa);", inst->rd);
    case SPEC_MTSA:
        return snprintf(buf, sz, "ctx->sa = GPR_U32(ctx, %u) & 0x1F;", inst->rs);

    /* 64-bit */
    case SPEC_DADD: case SPEC_DADDU:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) + GPR_U64(ctx, %u));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_DSUB: case SPEC_DSUBU:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) - GPR_U64(ctx, %u));",
                        inst->rd, inst->rs, inst->rt);
    case SPEC_DSLL:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) << %u);",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_DSRL:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) >> %u);",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_DSRA:
        return snprintf(buf, sz, "SET_GPR_S64(ctx, %u, GPR_S64(ctx, %u) >> %u);",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_DSLLV:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) << (GPR_U32(ctx, %u) & 0x3F));",
                        inst->rd, inst->rt, inst->rs);
    case SPEC_DSRLV:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) >> (GPR_U32(ctx, %u) & 0x3F));",
                        inst->rd, inst->rt, inst->rs);
    case SPEC_DSRAV:
        return snprintf(buf, sz, "SET_GPR_S64(ctx, %u, GPR_S64(ctx, %u) >> (GPR_U32(ctx, %u) & 0x3F));",
                        inst->rd, inst->rt, inst->rs);
    case SPEC_DSLL32:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) << (32 + %u));",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_DSRL32:
        return snprintf(buf, sz, "SET_GPR_U64(ctx, %u, GPR_U64(ctx, %u) >> (32 + %u));",
                        inst->rd, inst->rt, inst->sa);
    case SPEC_DSRA32:
        return snprintf(buf, sz, "SET_GPR_S64(ctx, %u, GPR_S64(ctx, %u) >> (32 + %u));",
                        inst->rd, inst->rt, inst->sa);

    /* Traps */
    case SPEC_TGE:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) >= GPR_S32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);
    case SPEC_TGEU:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) >= GPR_U32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);
    case SPEC_TLT:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) < GPR_S32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);
    case SPEC_TLTU:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) < GPR_U32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);
    case SPEC_TEQ:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) == GPR_U32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);
    case SPEC_TNE:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) != GPR_U32(ctx, %u)) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->rt);

    default:
        return snprintf(buf, sz, "// Unhandled SPECIAL: 0x%X", inst->function);
    }
}

/* =========================================================================
 * REGIMM instructions
 * ========================================================================= */

int recomp_translate_regimm(const r5900_inst_t *inst, char *buf, size_t sz)
{
    switch (inst->rt) {
    case REGIMM_BLTZ: case REGIMM_BGEZ:
    case REGIMM_BLTZL: case REGIMM_BGEZL:
    case REGIMM_BLTZAL: case REGIMM_BGEZAL:
    case REGIMM_BLTZALL: case REGIMM_BGEZALL:
    {
        uint32_t target = inst->address + 4 + (inst->simmediate << 2);
        return snprintf(buf, sz, "// REGIMM branch to 0x%X - Handled by branch logic", target);
    }

    case REGIMM_MTSAB:
        return snprintf(buf, sz, "ctx->sa = (GPR_U32(ctx, %u) + %d) & 0xF;",
                        inst->rs, inst->simmediate);
    case REGIMM_MTSAH:
        return snprintf(buf, sz, "ctx->sa = ((GPR_U32(ctx, %u) + %d) & 0x7) << 1;",
                        inst->rs, inst->simmediate);

    case REGIMM_TGEI:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) >= %d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);
    case REGIMM_TGEIU:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) >= (uint32_t)%d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);
    case REGIMM_TLTI:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) < %d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);
    case REGIMM_TLTIU:
        return snprintf(buf, sz, "if (GPR_U32(ctx, %u) < (uint32_t)%d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);
    case REGIMM_TEQI:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) == %d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);
    case REGIMM_TNEI:
        return snprintf(buf, sz, "if (GPR_S32(ctx, %u) != %d) { runtime->handleTrap(rdram, ctx); }",
                        inst->rs, inst->simmediate);

    default:
        return snprintf(buf, sz, "// Unhandled REGIMM: 0x%X", inst->rt);
    }
}

/* =========================================================================
 * COP0 instructions
 * ========================================================================= */

/** COP0 register name table for MFC0/MTC0 code generation */
static const char *cop0_reg_names[] = {
    "cop0_index",    "cop0_random",   "cop0_entrylo0", "cop0_entrylo1",
    "cop0_context",  "cop0_pagemask", "cop0_wired",    "cop0_reserved7",
    "cop0_badvaddr", "cop0_count",    "cop0_entryhi",  "cop0_compare",
    "cop0_status",   "cop0_cause",    "cop0_epc",      "cop0_prid",
    "cop0_config",   NULL, NULL, NULL, NULL, NULL, NULL,
    "cop0_badpaddr", "cop0_debug",    "cop0_perf",     NULL, NULL,
    "cop0_taglo",    "cop0_taghi",    "cop0_errorepc",  NULL
};

int recomp_translate_cop0(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t fmt = inst->rs;
    uint32_t rt  = inst->rt;
    uint32_t rd  = inst->rd;

    switch (fmt) {
    case COP0_MF:
        if (rd < 31 && cop0_reg_names[rd])
            return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->%s);", rt, cop0_reg_names[rd]);
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, 0); // Unimplemented COP0 reg %u", rt, rd);

    case COP0_MT:
        if (rd < 31 && cop0_reg_names[rd]) {
            /* Some COP0 regs have write masks */
            switch (rd) {
            case 1:  return snprintf(buf, sz, "// MTC0 to RANDOM (read-only)");
            case 8:  return snprintf(buf, sz, "// MTC0 to BADVADDR (read-only)");
            case 15: return snprintf(buf, sz, "// MTC0 to PRID (read-only)");
            case 12: /* STATUS */
                return snprintf(buf, sz, "ctx->cop0_status = GPR_U32(ctx, %u) & 0xFF57FFFF;", rt);
            case 13: /* CAUSE */
                return snprintf(buf, sz, "ctx->cop0_cause = (ctx->cop0_cause & ~0x00000300) | (GPR_U32(ctx, %u) & 0x00000300);", rt);
            case 11: /* COMPARE */
                return snprintf(buf, sz, "ctx->cop0_compare = GPR_U32(ctx, %u); ctx->cop0_cause &= ~0x8000;", rt);
            case 6:  /* WIRED */
                return snprintf(buf, sz, "ctx->cop0_wired = GPR_U32(ctx, %u) & 0x3F; ctx->cop0_random = 47;", rt);
            default:
                return snprintf(buf, sz, "ctx->%s = GPR_U32(ctx, %u);", cop0_reg_names[rd], rt);
            }
        }
        return snprintf(buf, sz, "// Unimplemented MTC0 to COP0 %u", rd);

    case COP0_BC:
        return snprintf(buf, sz, "// BC0 (cond: 0x%X) - Handled by branch logic", rt);

    case COP0_CO:
    {
        uint32_t func = inst->function;
        switch (func) {
        case COP0_TLBR:  return snprintf(buf, sz, "runtime->handleTLBR(rdram, ctx);");
        case COP0_TLBWI: return snprintf(buf, sz, "runtime->handleTLBWI(rdram, ctx);");
        case COP0_TLBWR: return snprintf(buf, sz, "runtime->handleTLBWR(rdram, ctx);");
        case COP0_TLBP:  return snprintf(buf, sz, "runtime->handleTLBP(rdram, ctx);");
        case COP0_ERET:
            return snprintf(buf, sz,
                "if (ctx->cop0_status & 0x4) { "
                "ctx->pc = ctx->cop0_errorepc; ctx->cop0_status &= ~0x4; "
                "} else { "
                "ctx->pc = ctx->cop0_epc; ctx->cop0_status &= ~0x2; "
                "} runtime->clearLLBit(ctx); return;");
        case COP0_EI:
            return snprintf(buf, sz, "ctx->cop0_status |= 0x1; // Enable interrupts");
        case COP0_DI:
            return snprintf(buf, sz, "ctx->cop0_status &= ~0x1; // Disable interrupts");
        default:
            return snprintf(buf, sz, "// Unhandled COP0 CO-OP: 0x%X", func);
        }
    }

    default:
        return snprintf(buf, sz, "// Unhandled COP0 format: 0x%X", fmt);
    }
}

/* =========================================================================
 * FPU (COP1) instructions
 * ========================================================================= */

int recomp_translate_fpu(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint8_t fmt = (uint8_t)inst->rs;  /* format field */
    uint32_t ft = inst->rt;           /* FPU rt */
    uint32_t fs = inst->rd;           /* FPU fs */
    uint32_t fd = inst->sa;           /* FPU fd */
    uint32_t func = inst->function;

    switch (fmt) {
    case COP1_MF:
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, *(uint32_t*)&ctx->f[%u]);", ft, fs);
    case COP1_MT:
        return snprintf(buf, sz, "*(uint32_t*)&ctx->f[%u] = GPR_U32(ctx, %u);", fs, ft);
    case COP1_CF:
        if (fs == 31) return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->fcr31);", ft);
        if (fs == 0)  return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, 0x00000000);", ft);
        return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, 0); // Unimpl FCR%u", ft, fs);
    case COP1_CT:
        if (fs == 31) return snprintf(buf, sz, "ctx->fcr31 = GPR_U32(ctx, %u) & 0x0183FFFF;", ft);
        return snprintf(buf, sz, "// CTC1 to FCR%u ignored", fs);
    case COP1_BC:
        return snprintf(buf, sz, "// FPU branch - handled elsewhere");

    case COP1_S:
        switch (func) {
        case COP1S_ADD:  return snprintf(buf, sz, "ctx->f[%u] = FPU_ADD_S(ctx->f[%u], ctx->f[%u]);", fd, fs, ft);
        case COP1S_SUB:  return snprintf(buf, sz, "ctx->f[%u] = FPU_SUB_S(ctx->f[%u], ctx->f[%u]);", fd, fs, ft);
        case COP1S_MUL:  return snprintf(buf, sz, "ctx->f[%u] = FPU_MUL_S(ctx->f[%u], ctx->f[%u]);", fd, fs, ft);
        case COP1S_DIV:
            return snprintf(buf, sz,
                "if (ctx->f[%u] == 0.0f) { ctx->fcr31 |= 0x100000; "
                "ctx->f[%u] = copysignf(INFINITY, ctx->f[%u] * 0.0f); } "
                "else ctx->f[%u] = ctx->f[%u] / ctx->f[%u];",
                ft, fd, fs, fd, fs, ft);
        case COP1S_SQRT: return snprintf(buf, sz, "ctx->f[%u] = FPU_SQRT_S(ctx->f[%u]);", fd, fs);
        case COP1S_ABS:  return snprintf(buf, sz, "ctx->f[%u] = FPU_ABS_S(ctx->f[%u]);", fd, fs);
        case COP1S_MOV:  return snprintf(buf, sz, "ctx->f[%u] = FPU_MOV_S(ctx->f[%u]);", fd, fs);
        case COP1S_NEG:  return snprintf(buf, sz, "ctx->f[%u] = FPU_NEG_S(ctx->f[%u]);", fd, fs);

        case COP1S_ROUND_W: return snprintf(buf, sz, "*(int32_t*)&ctx->f[%u] = FPU_ROUND_W_S(ctx->f[%u]);", fd, fs);
        case COP1S_TRUNC_W: return snprintf(buf, sz, "*(int32_t*)&ctx->f[%u] = FPU_TRUNC_W_S(ctx->f[%u]);", fd, fs);
        case COP1S_CEIL_W:  return snprintf(buf, sz, "*(int32_t*)&ctx->f[%u] = FPU_CEIL_W_S(ctx->f[%u]);", fd, fs);
        case COP1S_FLOOR_W: return snprintf(buf, sz, "*(int32_t*)&ctx->f[%u] = FPU_FLOOR_W_S(ctx->f[%u]);", fd, fs);
        case COP1S_CVT_W:   return snprintf(buf, sz, "*(int32_t*)&ctx->f[%u] = FPU_CVT_W_S(ctx->f[%u]);", fd, fs);
        case COP1S_RSQRT:   return snprintf(buf, sz, "ctx->f[%u] = 1.0f / sqrtf(ctx->f[%u]);", fd, fs);

        /* PS2-specific accumulator FPU ops */
        case COP1S_ADDA:  return snprintf(buf, sz, "ctx->f[31] = FPU_ADD_S(ctx->f[%u], ctx->f[%u]);", fs, ft);
        case COP1S_SUBA:  return snprintf(buf, sz, "ctx->f[31] = FPU_SUB_S(ctx->f[%u], ctx->f[%u]);", fs, ft);
        case COP1S_MULA:  return snprintf(buf, sz, "ctx->f[31] = FPU_MUL_S(ctx->f[%u], ctx->f[%u]);", fs, ft);
        case COP1S_MADD:  return snprintf(buf, sz, "ctx->f[%u] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[%u], ctx->f[%u]));", fd, fs, ft);
        case COP1S_MSUB:  return snprintf(buf, sz, "ctx->f[%u] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[%u], ctx->f[%u]));", fd, fs, ft);
        case COP1S_MADDA: return snprintf(buf, sz, "ctx->f[31] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[%u], ctx->f[%u]));", fs, ft);
        case COP1S_MSUBA: return snprintf(buf, sz, "ctx->f[31] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[%u], ctx->f[%u]));", fs, ft);

        case COP1S_MAX:   return snprintf(buf, sz, "ctx->f[%u] = fmaxf(ctx->f[%u], ctx->f[%u]);", fd, fs, ft);
        case COP1S_MIN:   return snprintf(buf, sz, "ctx->f[%u] = fminf(ctx->f[%u], ctx->f[%u]);", fd, fs, ft);

        /* FPU comparisons — set/clear bit 23 of FCR31 */
        case COP1S_C_F:   return snprintf(buf, sz, "ctx->fcr31 &= ~0x800000;");
        case COP1S_C_UN:  return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_UN_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_EQ:  return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_EQ_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_UEQ: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_UEQ_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_OLT: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_OLT_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_ULT: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_ULT_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_OLE: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_OLE_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_ULE: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_ULE_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_SF:  return snprintf(buf, sz, "ctx->fcr31 &= ~0x800000;");
        case COP1S_C_NGLE:return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_NGLE_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_SEQ: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_SEQ_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_NGL: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_NGL_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_LT:  return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_LT_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_NGE: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_NGE_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_LE:  return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_LE_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
        case COP1S_C_NGT: return snprintf(buf, sz, "ctx->fcr31 = (FPU_C_NGT_S(ctx->f[%u], ctx->f[%u])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);

        default:
            return snprintf(buf, sz, "// Unhandled FPU.S func: 0x%X", func);
        }

    case COP1_W:
        if (func == COP1W_CVT_S)
            return snprintf(buf, sz, "ctx->f[%u] = FPU_CVT_S_W(*(int32_t*)&ctx->f[%u]);", fd, fs);
        return snprintf(buf, sz, "// Unhandled FPU.W func: 0x%X", func);

    default:
        return snprintf(buf, sz, "// Unhandled FPU format: 0x%X", fmt);
    }
}

/* =========================================================================
 * VU (COP2) instructions
 * ========================================================================= */

int recomp_translate_vu(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint8_t fmt = (uint8_t)inst->rs;
    uint32_t rt = inst->rt;
    uint32_t rd = inst->rd;

    switch (fmt) {
    case COP2_QMFC2:
        return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, VEC_CAST_F2I(ctx->vu0_vf[%u]));", rt, rd);
    case COP2_QMTC2:
        return snprintf(buf, sz, "ctx->vu0_vf[%u] = VEC_CAST_I2F(GPR_VEC(ctx, %u));", rd, rt);

    case COP2_CFC2:
        /* Read VU0 control register */
        switch (rd) {
        case VU0CR_STATUS:    return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_status);", rt);
        case VU0CR_MAC:       return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_mac_flags);", rt);
        case VU0CR_VPU_STAT:  return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_vpu_stat);", rt);
        case VU0CR_CLIP:      return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_clip_flags);", rt);
        case VU0CR_R:         return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, VEC_CAST_F2I(ctx->vu0_r));", rt);
        case VU0CR_I:         return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, *(uint32_t*)&ctx->vu0_i);", rt);
        case VU0CR_TPC:       return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_tpc);", rt);
        case VU0CR_CMSAR0:    return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_cmsar0);", rt);
        case VU0CR_FBRST:     return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_fbrst);", rt);
        case VU0CR_ACC:       return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, VEC_CAST_F2I(ctx->vu0_acc));", rt);
        case VU0CR_P:         return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, *(uint32_t*)&ctx->vu0_p);", rt);
        case VU0CR_ITOP:      return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->vu0_itop);", rt);
        default:
            return snprintf(buf, sz, "// Unimplemented CFC2 VU CReg: %u", rd);
        }

    case COP2_CTC2:
        /* Write VU0 control register */
        switch (rd) {
        case VU0CR_STATUS:    return snprintf(buf, sz, "ctx->vu0_status = GPR_U32(ctx, %u) & 0xFFFF;", rt);
        case VU0CR_MAC:       return snprintf(buf, sz, "ctx->vu0_mac_flags = GPR_U32(ctx, %u);", rt);
        case VU0CR_VPU_STAT:  return snprintf(buf, sz, "ctx->vu0_vpu_stat = GPR_U32(ctx, %u);", rt);
        case VU0CR_CLIP:      return snprintf(buf, sz, "ctx->vu0_clip_flags = GPR_U32(ctx, %u);", rt);
        case VU0CR_R:         return snprintf(buf, sz, "ctx->vu0_r = VEC_CAST_I2F(GPR_VEC(ctx, %u));", rt);
        case VU0CR_I:         return snprintf(buf, sz, "{ uint32_t tmp = GPR_U32(ctx, %u); memcpy(&ctx->vu0_i, &tmp, 4); }", rt);
        case VU0CR_TPC:       return snprintf(buf, sz, "ctx->vu0_tpc = GPR_U32(ctx, %u);", rt);
        case VU0CR_CMSAR0:    return snprintf(buf, sz, "ctx->vu0_cmsar0 = GPR_U32(ctx, %u);", rt);
        case VU0CR_FBRST:     return snprintf(buf, sz, "ctx->vu0_fbrst = GPR_U32(ctx, %u);", rt);
        case VU0CR_ACC:       return snprintf(buf, sz, "ctx->vu0_acc = VEC_CAST_I2F(GPR_VEC(ctx, %u));", rt);
        case VU0CR_P:         return snprintf(buf, sz, "{ uint32_t tmp = GPR_U32(ctx, %u); memcpy(&ctx->vu0_p, &tmp, 4); }", rt);
        case VU0CR_ITOP:      return snprintf(buf, sz, "ctx->vu0_itop = GPR_U32(ctx, %u) & 0x3FF;", rt);
        default:
            return snprintf(buf, sz, "// Unimplemented CTC2 VU CReg: %u", rd);
        }

    case COP2_BC:
        return snprintf(buf, sz, "// BC2 (cond: 0x%X) - Handled by branch logic", rt);

    default:
        /* COP2 CO — VU0 macro mode operations */
        if (fmt >= COP2_CO) {
            uint8_t vu_func = (uint8_t)inst->function;
            uint32_t vd = inst->rd;
            uint32_t vs = inst->rs;
            uint32_t vt = inst->rt;

            if (vu_func >= 0x3C) {
                /* Special2 table */
                switch (vu_func) {
                case VU0S2_VDIV:
                    return snprintf(buf, sz,
                        "{ float fs_val = VU_FIELD(ctx->vu0_vf[%u], %u); "
                        "float ft_val = VU_FIELD(ctx->vu0_vf[%u], %u); "
                        "ctx->vu0_q = (ft_val != 0.0f) ? (fs_val / ft_val) : 0.0f; }",
                        inst->rs, inst->vu_fsf, inst->rt, inst->vu_ftf);
                case VU0S2_VSQRT:
                    return snprintf(buf, sz,
                        "{ float ft_val = VU_FIELD(ctx->vu0_vf[%u], %u); "
                        "ctx->vu0_q = sqrtf(ft_val > 0.0f ? ft_val : 0.0f); }",
                        inst->rt, inst->vu_ftf);
                case VU0S2_VRSQRT:
                    return snprintf(buf, sz,
                        "{ float ft_val = VU_FIELD(ctx->vu0_vf[%u], %u); "
                        "ctx->vu0_q = (ft_val > 0.0f) ? (1.0f / sqrtf(ft_val)) : 0.0f; }",
                        inst->rt, inst->vu_ftf);
                case VU0S2_VWAITQ:
                    return snprintf(buf, sz, "// VWAITQ (NOP in recompiler)");
                case VU0S2_VMTIR:
                    return snprintf(buf, sz, "ctx->vu0_i = (float)ctx->vi[%u];", vt);
                case VU0S2_VMFIR:
                    return snprintf(buf, sz,
                        "{ float val = (float)ctx->vi[%u]; "
                        "VU_SET_FIELD(ctx->vu0_vf[%u], val, 0x%X); }",
                        vs, vt, inst->vu_dest);
                case VU0S2_VILWR:
                    return snprintf(buf, sz, "// VILWR vi[%u] <- mem[vs]", vt);
                case VU0S2_VISWR:
                    return snprintf(buf, sz, "// VISWR vi[%u] -> mem[vs]", vt);
                case VU0S2_VNOP:
                    return snprintf(buf, sz, "// VNOP");
                case VU0S2_VMOVE:
                    return snprintf(buf, sz, "ctx->vu0_vf[%u] = ctx->vu0_vf[%u];", vt, vs);
                case VU0S2_VABS:
                    return snprintf(buf, sz, "ctx->vu0_vf[%u] = VU_ABS(ctx->vu0_vf[%u]);", vt, vs);
                case VU0S2_VMR32:
                    return snprintf(buf, sz, "ctx->vu0_vf[%u] = VU_ROTATE32(ctx->vu0_vf[%u]);", vt, vs);
                case VU0S2_VRNEXT:
                    return snprintf(buf, sz, "// VRNEXT (random number advance)");
                case VU0S2_VRGET:
                    return snprintf(buf, sz,
                        "VU_SET_FIELD(ctx->vu0_vf[%u], ctx->vu0_r, 0x%X);",
                        vt, inst->vu_dest);
                case VU0S2_VRINIT:
                    return snprintf(buf, sz, "// VRINIT (random seed from vf[%u])", vs);
                case VU0S2_VRXOR:
                    return snprintf(buf, sz, "// VRXOR (random XOR from vf[%u])", vs);
                default:
                    return snprintf(buf, sz, "// Unhandled VU0 Special2: 0x%X", vu_func);
                }
            } else {
                /* Special1 table */
                switch (vu_func) {
                case VU0S1_VADDx: case VU0S1_VADDy: case VU0S1_VADDz: case VU0S1_VADDw:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VADD, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VSUBx: case VU0S1_VSUBy: case VU0S1_VSUBz: case VU0S1_VSUBw:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VSUB, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VMULx: case VU0S1_VMULy: case VU0S1_VMULz: case VU0S1_VMULw:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VMUL, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VADD:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VADD, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VSUB:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VSUB, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VMUL:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], PS2_VMUL, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);

                case VU0S1_VADDq:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VADD(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_q));",
                        vd, vs);
                case VU0S1_VSUBq:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VSUB(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_q));",
                        vd, vs);
                case VU0S1_VMULq:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VMUL(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_q));",
                        vd, vs);
                case VU0S1_VADDi:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VADD(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_i));",
                        vd, vs);
                case VU0S1_VSUBi:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VSUB(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_i));",
                        vd, vs);
                case VU0S1_VMULi:
                    return snprintf(buf, sz,
                        "ctx->vu0_vf[%u] = PS2_VMUL(ctx->vu0_vf[%u], VEC_SPLAT(ctx->vu0_i));",
                        vd, vs);

                case VU0S1_VIADD:
                    return snprintf(buf, sz, "ctx->vi[%u] = ctx->vi[%u] + ctx->vi[%u];", vd, vs, vt);
                case VU0S1_VISUB:
                    return snprintf(buf, sz, "ctx->vi[%u] = ctx->vi[%u] - ctx->vi[%u];", vd, vs, vt);
                case VU0S1_VIADDI:
                    return snprintf(buf, sz, "ctx->vi[%u] = ctx->vi[%u] + %u;", vt, vs, inst->sa);
                case VU0S1_VIAND:
                    return snprintf(buf, sz, "ctx->vi[%u] = ctx->vi[%u] & ctx->vi[%u];", vd, vs, vt);
                case VU0S1_VIOR:
                    return snprintf(buf, sz, "ctx->vi[%u] = ctx->vi[%u] | ctx->vi[%u];", vd, vs, vt);

                case VU0S1_VCALLMS:
                {
                    uint16_t idx = inst->immediate & 0x1FF;
                    uint32_t addr = (uint32_t)idx << 3;
                    return snprintf(buf, sz,
                        "{ ctx->vu0_tpc = 0x%X; runtime->executeVU0Microprogram(rdram, ctx, 0x%X); }",
                        addr, addr);
                }
                case VU0S1_VCALLMSR:
                    return snprintf(buf, sz,
                        "{ uint16_t idx = ctx->vi[%u] & 0x1FF; uint32_t addr = (uint32_t)idx << 3; "
                        "ctx->vu0_pc = addr; runtime->vu0StartMicroProgram(rdram, ctx, addr); }",
                        vs);

                /* VMADD field variants */
                case VU0S1_VMADDx: case VU0S1_VMADDy: case VU0S1_VMADDz: case VU0S1_VMADDw:
                    return snprintf(buf, sz,
                        "{ ps2_vec_t mul = PS2_VMUL(ctx->vu0_vf[%u], ctx->vu0_vf[%u]); "
                        "ps2_vec_t res = PS2_VADD(ctx->vu0_acc, mul); "
                        "VU_SET_MASKED(ctx->vu0_vf[%u], res, 0x%X); ctx->vu0_acc = res; }",
                        vs, vt, vd, inst->vu_dest);
                case VU0S1_VMADD:
                    return snprintf(buf, sz,
                        "{ ps2_vec_t mul = PS2_VMUL(ctx->vu0_vf[%u], ctx->vu0_vf[%u]); "
                        "ps2_vec_t res = PS2_VADD(ctx->vu0_acc, mul); "
                        "VU_SET_MASKED(ctx->vu0_vf[%u], res, 0x%X); ctx->vu0_acc = res; }",
                        vs, vt, vd, inst->vu_dest);
                case VU0S1_VMAX:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], VEC_MAX, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);
                case VU0S1_VOPMSUB:
                    return snprintf(buf, sz,
                        "{ ps2_vec_t mul = PS2_VMUL(ctx->vu0_vf[%u], ctx->vu0_vf[%u]); "
                        "ps2_vec_t res = PS2_VSUB(ctx->vu0_acc, mul); "
                        "VU_SET_MASKED(ctx->vu0_vf[%u], res, 0x%X); ctx->vu0_acc = res; }",
                        vs, vt, vd, inst->vu_dest);
                case VU0S1_VMINI:
                    return snprintf(buf, sz,
                        "VU_OP_FIELD(ctx->vu0_vf[%u], VEC_MIN, ctx->vu0_vf[%u], ctx->vu0_vf[%u], 0x%X);",
                        vd, vs, vt, inst->vu_dest);

                default:
                    return snprintf(buf, sz, "// Unhandled VU0 Special1: 0x%X", vu_func);
                }
            }
        }
        return snprintf(buf, sz, "// Unhandled COP2 format: 0x%X", fmt);
    }
}

/* =========================================================================
 * MMI instructions
 * ========================================================================= */

int recomp_translate_mmi(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t func = inst->function;
    uint32_t rs = inst->rs, rt = inst->rt, rd = inst->rd, sa = inst->sa;

    switch (func) {
    case MMI_MFHI1: return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->hi1);", rd);
    case MMI_MTHI1: return snprintf(buf, sz, "ctx->hi1 = GPR_U32(ctx, %u);", rs);
    case MMI_MFLO1: return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->lo1);", rd);
    case MMI_MTLO1: return snprintf(buf, sz, "ctx->lo1 = GPR_U32(ctx, %u);", rs);

    case MMI_MULT1:
        return snprintf(buf, sz,
            "{ int64_t result = (int64_t)GPR_S32(ctx, %u) * (int64_t)GPR_S32(ctx, %u); "
            "ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MULTU1:
        return snprintf(buf, sz,
            "{ uint64_t result = (uint64_t)GPR_U32(ctx, %u) * (uint64_t)GPR_U32(ctx, %u); "
            "ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_DIV1:
        return snprintf(buf, sz,
            "{ int32_t divisor = GPR_S32(ctx, %u); "
            "if (divisor != 0) { ctx->lo1 = (uint32_t)(GPR_S32(ctx, %u) / divisor); "
            "ctx->hi1 = (uint32_t)(GPR_S32(ctx, %u) %% divisor); } "
            "else { ctx->lo1 = (GPR_S32(ctx,%u) < 0) ? 1 : -1; ctx->hi1 = GPR_S32(ctx,%u); } }",
            rt, rs, rs, rs, rs);
    case MMI_DIVU1:
        return snprintf(buf, sz,
            "{ uint32_t divisor = GPR_U32(ctx, %u); "
            "if (divisor != 0) { ctx->lo1 = GPR_U32(ctx, %u) / divisor; "
            "ctx->hi1 = GPR_U32(ctx, %u) %% divisor; } "
            "else { ctx->lo1 = 0xFFFFFFFF; ctx->hi1 = GPR_U32(ctx,%u); } }",
            rt, rs, rs, rs);

    case MMI_MADD:
        return snprintf(buf, sz,
            "{ int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; "
            "int64_t prod = (int64_t)GPR_S32(ctx, %u) * (int64_t)GPR_S32(ctx, %u); "
            "int64_t result = acc + prod; "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MADDU:
        return snprintf(buf, sz,
            "{ uint64_t acc = ((uint64_t)ctx->hi << 32) | ctx->lo; "
            "uint64_t prod = (uint64_t)GPR_U32(ctx, %u) * (uint64_t)GPR_U32(ctx, %u); "
            "uint64_t result = acc + prod; "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MSUB:
        return snprintf(buf, sz,
            "{ int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; "
            "int64_t prod = (int64_t)GPR_S32(ctx, %u) * (int64_t)GPR_S32(ctx, %u); "
            "int64_t result = acc - prod; "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MSUBU:
        return snprintf(buf, sz,
            "{ uint64_t acc = ((uint64_t)ctx->hi << 32) | ctx->lo; "
            "uint64_t prod = (uint64_t)GPR_U32(ctx, %u) * (uint64_t)GPR_U32(ctx, %u); "
            "uint64_t result = acc - prod; "
            "ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MADD1:
        return snprintf(buf, sz,
            "{ int64_t acc = ((int64_t)ctx->hi1 << 32) | ctx->lo1; "
            "int64_t prod = (int64_t)GPR_S32(ctx, %u) * (int64_t)GPR_S32(ctx, %u); "
            "int64_t result = acc + prod; "
            "ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }",
            rs, rt);
    case MMI_MADDU1:
        return snprintf(buf, sz,
            "{ uint64_t acc = ((uint64_t)ctx->hi1 << 32) | ctx->lo1; "
            "uint64_t prod = (uint64_t)GPR_U32(ctx, %u) * (uint64_t)GPR_U32(ctx, %u); "
            "uint64_t result = acc + prod; "
            "ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }",
            rs, rt);

    case MMI_PLZCW:
        return snprintf(buf, sz, "{ uint32_t val = GPR_U32(ctx, %u); SET_GPR_U32(ctx, %u, ps2_clz32(val)); }", rs, rd);

    /* Vector shifts */
    case MMI_PSLLH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_LEFT_H(GPR_VEC(ctx, %u), %u));", rd, rt, sa);
    case MMI_PSRLH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_RIGHT_H(GPR_VEC(ctx, %u), %u));", rd, rt, sa);
    case MMI_PSRAH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_ARITH_H(GPR_VEC(ctx, %u), %u));", rd, rt, sa);
    case MMI_PSLLW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_LEFT_W(GPR_VEC(ctx, %u), %u));", rd, rt, sa);
    case MMI_PSRLW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_RIGHT_W(GPR_VEC(ctx, %u), %u));", rd, rt, sa);
    case MMI_PSRAW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_SHIFT_ARITH_W(GPR_VEC(ctx, %u), %u));", rd, rt, sa);

    case MMI_PMFHL: return recomp_translate_pmfhl(inst, buf, sz);
    case MMI_PMTHL:
        return snprintf(buf, sz,
            "{ ps2_vec128_t val = GPR_VEC(ctx, %u); "
            "ctx->lo = VEC_ELEM_U32(val, 0); ctx->hi = VEC_ELEM_U32(val, 1); }",
            rs);

    case MMI_MMI0: return recomp_translate_mmi0(inst, buf, sz);
    case MMI_MMI1: return recomp_translate_mmi1(inst, buf, sz);
    case MMI_MMI2: return recomp_translate_mmi2(inst, buf, sz);
    case MMI_MMI3: return recomp_translate_mmi3(inst, buf, sz);

    default:
        return snprintf(buf, sz, "// Unhandled MMI func: 0x%X", func);
    }
}

/* =========================================================================
 * MMI0 sub-group
 * ========================================================================= */

int recomp_translate_mmi0(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t sub = inst->sa;
    uint32_t rs = inst->rs, rt = inst->rt, rd = inst->rd;

    switch (sub) {
    case MMI0_PADDW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PCGTW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCGTW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PMAXW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMAXW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PADDH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PCGTH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCGTH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PMAXH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMAXH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PADDB:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBB:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PCGTB:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCGTB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PADDSW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDSW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBSW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBSW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PEXTLW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTLW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PPACW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PPACW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PADDSH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDSH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBSH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBSH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PEXTLH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTLH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PPACH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PPACH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PADDSB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDSB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PSUBSB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBSB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PEXTLB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTLB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PPACB:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PPACB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI0_PEXT5:  return snprintf(buf, sz, "// TODO PEXT5");
    case MMI0_PPAC5:  return snprintf(buf, sz, "// TODO PPAC5");
    default:
        return snprintf(buf, sz, "// Unhandled MMI0: 0x%X", sub);
    }
}

/* =========================================================================
 * MMI1 sub-group
 * ========================================================================= */

int recomp_translate_mmi1(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t sub = inst->sa;
    uint32_t rs = inst->rs, rt = inst->rt, rd = inst->rd;

    switch (sub) {
    case MMI1_PABSW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PABSW(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI1_PCEQW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCEQW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PMINW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMINW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PADSBH: return snprintf(buf, sz, "// TODO PADSBH");
    case MMI1_PABSH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PABSH(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI1_PCEQH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCEQH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PMINH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMINH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PCEQB:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCEQB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PADDUW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDUW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PSUBUW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBUW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PEXTUW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTUW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PADDUH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDUH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PSUBUH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBUH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PEXTUH: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTUH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PADDUB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PADDUB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PSUBUB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSUBUB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_PEXTUB: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXTUB(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI1_QFSRV:
        return snprintf(buf, sz,
            "SET_GPR_VEC(ctx, %u, PS2_QFSRV(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u), ctx->sa));",
            rd, rs, rt);
    default:
        return snprintf(buf, sz, "// Unhandled MMI1: 0x%X", sub);
    }
}

/* =========================================================================
 * MMI2 sub-group
 * ========================================================================= */

int recomp_translate_mmi2(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t sub = inst->sa;
    uint32_t rs = inst->rs, rt = inst->rt, rd = inst->rd;

    switch (sub) {
    case MMI2_PMADDW: return snprintf(buf, sz, "PS2_PMADDW_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI2_PSLLVW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSLLVW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PSRLVW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSRLVW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PMSUBW: return snprintf(buf, sz, "// TODO PMSUBW");
    case MMI2_PMFHI:  return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->hi);", rd);
    case MMI2_PMFLO:  return snprintf(buf, sz, "SET_GPR_U32(ctx, %u, ctx->lo);", rd);
    case MMI2_PINTH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PINTH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PMULTW: return snprintf(buf, sz, "// TODO PMULTW");
    case MMI2_PDIVW:
        return snprintf(buf, sz,
            "{ int32_t rs0 = GPR_S32(ctx, %u); int32_t rt0 = GPR_S32(ctx, %u); "
            "if (rt0 != 0) { ctx->lo = (uint32_t)(rs0 / rt0); ctx->hi = (uint32_t)(rs0 %% rt0); } "
            "else { ctx->lo = (rs0 < 0) ? 1 : -1; ctx->hi = rs0; } "
            "SET_GPR_U32(ctx, %u, ctx->lo); }",
            rs, rt, rd);
    case MMI2_PCPYLD:
        return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCPYLD(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PAND:   return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PAND(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PXOR:   return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PXOR(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI2_PMADDH: return snprintf(buf, sz, "PS2_PMADDH_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI2_PHMADH: return snprintf(buf, sz, "PS2_PHMADH_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI2_PMSUBH: return snprintf(buf, sz, "// TODO PMSUBH");
    case MMI2_PHMSBH: return snprintf(buf, sz, "// TODO PHMSBH");
    case MMI2_PEXEH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXEH(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI2_PREVH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PREVH(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI2_PMULTH: return snprintf(buf, sz, "PS2_PMULTH_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI2_PDIVBW: return snprintf(buf, sz, "PS2_PDIVBW_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI2_PEXEW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXEW(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI2_PROT3W: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PROT3W(GPR_VEC(ctx, %u)));", rd, rs);
    default:
        return snprintf(buf, sz, "// Unhandled MMI2: 0x%X", sub);
    }
}

/* =========================================================================
 * MMI3 sub-group
 * ========================================================================= */

int recomp_translate_mmi3(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t sub = inst->sa;
    uint32_t rs = inst->rs, rt = inst->rt, rd = inst->rd;

    switch (sub) {
    case MMI3_PMADDUW: return snprintf(buf, sz, "// TODO PMADDUW");
    case MMI3_PSRAVW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PSRAVW(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI3_PMTHI:   return snprintf(buf, sz, "ctx->hi = GPR_U32(ctx, %u);", rs);
    case MMI3_PMTLO:   return snprintf(buf, sz, "ctx->lo = GPR_U32(ctx, %u);", rs);
    case MMI3_PINTEH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PINTEH(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI3_PMULTUW: return snprintf(buf, sz, "PS2_PMULTUW_FULL(ctx, %u, %u, %u);", rs, rt, rd);
    case MMI3_PDIVUW:
        return snprintf(buf, sz,
            "{ uint32_t rs0 = GPR_U32(ctx, %u); uint32_t rt0 = GPR_U32(ctx, %u); "
            "if (rt0 != 0) { ctx->lo = rs0 / rt0; ctx->hi = rs0 %% rt0; } "
            "else { ctx->lo = 0xFFFFFFFF; ctx->hi = rs0; } "
            "SET_GPR_U32(ctx, %u, ctx->lo); }",
            rs, rt, rd);
    case MMI3_PCPYUD:
        return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCPYUD(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI3_POR:    return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_POR(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI3_PNOR:   return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PNOR(GPR_VEC(ctx, %u), GPR_VEC(ctx, %u)));", rd, rs, rt);
    case MMI3_PEXCH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXCH(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI3_PCPYH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PCPYH(GPR_VEC(ctx, %u)));", rd, rs);
    case MMI3_PEXCW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PEXCW(GPR_VEC(ctx, %u)));", rd, rs);
    default:
        return snprintf(buf, sz, "// Unhandled MMI3: 0x%X", sub);
    }
}

/* =========================================================================
 * PMFHL variants
 * ========================================================================= */

int recomp_translate_pmfhl(const r5900_inst_t *inst, char *buf, size_t sz)
{
    uint32_t sub = inst->sa;
    switch (sub) {
    case PMFHL_LW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMFHL_LW(ctx->hi, ctx->lo));", inst->rd);
    case PMFHL_UW:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMFHL_UW(ctx->hi, ctx->lo));", inst->rd);
    case PMFHL_SLW: return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMFHL_SLW(ctx->hi, ctx->lo));", inst->rd);
    case PMFHL_LH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMFHL_LH(ctx->hi, ctx->lo));", inst->rd);
    case PMFHL_SH:  return snprintf(buf, sz, "SET_GPR_VEC(ctx, %u, PS2_PMFHL_SH(ctx->hi, ctx->lo));", inst->rd);
    default:
        return snprintf(buf, sz, "// Unhandled PMFHL variant: 0x%X", sub);
    }
}

/* =========================================================================
 * File output — batch translation
 * ========================================================================= */

void recomp_translate_to_file(const r5900_inst_t *insts, size_t count,
                              const char *func_name, const char *output_path)
{
    if (!insts || count == 0 || !output_path) return;

    FILE *f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "Error opening output: %s\n", output_path);
        return;
    }

    /* Header */
    fprintf(f, "/* Auto-generated by PSRETROX recompiler */\n");
    fprintf(f, "#include \"ps2_runtime_macros.h\"\n");
    fprintf(f, "#include \"ps2_runtime.h\"\n\n");

    fprintf(f, "void %s(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) {\n\n",
            func_name ? func_name : "recompiled_func");

    char line[RECOMP_LINE_MAX];

    for (size_t i = 0; i < count; i++) {
        const r5900_inst_t *inst = &insts[i];

        fprintf(f, "    // 0x%08X: 0x%08X\n", inst->address, inst->raw);

        recomp_translate(inst, line, sizeof(line));
        fprintf(f, "    %s\n", line);
    }

    fprintf(f, "}\n");
    fclose(f);

    printf("Recompiled %zu instructions -> %s\n", count, output_path);
}

void recomp_binary_to_c(const uint8_t *code, size_t code_size,
                         uint32_t base_addr, const char *func_name,
                         const char *output_path)
{
    if (!code || code_size < 4) return;

    size_t count = code_size / 4;
    r5900_inst_t *insts = (r5900_inst_t *)calloc(count, sizeof(r5900_inst_t));
    if (!insts) {
        fprintf(stderr, "recomp_binary_to_c: allocation failed\n");
        return;
    }

    size_t decoded = 0;
    r5900_decode_buffer(code, code_size, base_addr, insts, &decoded);
    recomp_translate_to_file(insts, decoded, func_name, output_path);

    free(insts);
}

/* =========================================================================
 * Legacy reference: original tools/convert.c (x86 asm → pseudo-C)
 *
 * The old convert.c only handled x86 assembly (mov, push, jmp, etc.)
 * and was designed for a completely different pipeline. It is now superseded
 * by this recompiler which handles native R5900 MIPS instructions.
 *
 * Key functions from the old code:
 *   process_instruction(line, output) — matched ~80 x86 instructions
 *   convert_to_c(input_path, output_path) — line-by-line translation
 *
 * See git history for the full original source.
 * ========================================================================= */
