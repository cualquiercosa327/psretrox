/**
 * @file r5900_decompiler.c
 * @brief R5900 (EE) instruction decoder and disassembler — pure C.
 *
 * Migrated from ps2recomp's r5900_decoder.cpp (C++) to plain C.
 * This file replaces the old tools/disasm.c (which used Capstone for x86).
 * Now decodes MIPS R5900 natively without any external dependency.
 *
 * References:
 *   - ps2recomp  ps2xRecomp/src/r5900_decoder.cpp  (original C++ source)
 *   - "See MIPS Run" (Dominic Sweetman) — instruction encoding details
 *   - DarrenRainey | PS2-Programming-Docs - http://github.com/DarrenRainey/PS2-Programming-Docs/blob/master/EE_Core_Instruction_Set_Manual.pdf
 *   - Copetti PS2 Architecture — high-level overview of EE pipeline
 *
 * Original disasm.c (Capstone-based) kept below for study reference.
 */

#include "../include/r5900_decompiler.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Register name tables
 * ========================================================================= */

static const char *gpr_names[32] = {
    "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
    "$t0",   "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
    "$s0",   "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
    "$t8",   "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
};

/* =========================================================================
 * Internal helpers — decode sub-groups
 * Ported from R5900Decoder::decodeSpecial / decodeRegimm / etc.
 * ========================================================================= */

static void decode_special(r5900_inst_t *inst)
{
    if (inst->rd != 0)
        inst->flags.modifies_gpr = true;

    switch (inst->function) {
    case SPEC_JR:
        inst->flags.is_jump = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        if (inst->rs == 31)
            inst->flags.is_return = true;
        break;

    case SPEC_JALR:
        inst->flags.is_jump = true;
        inst->flags.is_call = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_gpr = (inst->rd != 0);
        inst->flags.modifies_control = true;
        break;

    case SPEC_SYSCALL:
    case SPEC_BREAK:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case SPEC_MFHI:
    case SPEC_MFLO:
        if (inst->rd == 0) inst->flags.modifies_gpr = false;
        break;

    case SPEC_MTHI:
    case SPEC_MTLO:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case SPEC_MULT:  case SPEC_MULTU:
    case SPEC_DIV:   case SPEC_DIVU:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true; /* HI/LO */
        break;

    case SPEC_ADD:  case SPEC_ADDU:
    case SPEC_SUB:  case SPEC_SUBU:
    case SPEC_AND:  case SPEC_OR:
    case SPEC_XOR:  case SPEC_NOR:
    case SPEC_SLL:  case SPEC_SRL:  case SPEC_SRA:
    case SPEC_SLLV: case SPEC_SRLV: case SPEC_SRAV:
    case SPEC_MOVZ: case SPEC_MOVN:
    case SPEC_SLT:  case SPEC_SLTU:
        if (inst->rd == 0) inst->flags.modifies_gpr = false;
        break;

    /* 64-bit operations */
    case SPEC_DADD:  case SPEC_DADDU:
    case SPEC_DSUB:  case SPEC_DSUBU:
    case SPEC_DSLL:  case SPEC_DSRL:  case SPEC_DSRA:
    case SPEC_DSLL32:case SPEC_DSRL32:case SPEC_DSRA32:
    case SPEC_DSLLV: case SPEC_DSRLV: case SPEC_DSRAV:
        if (inst->rd == 0) inst->flags.modifies_gpr = false;
        break;

    /* Traps */
    case SPEC_TGE:  case SPEC_TGEU:
    case SPEC_TLT:  case SPEC_TLTU:
    case SPEC_TEQ:  case SPEC_TNE:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    /* PS2 EE-specific SA register */
    case SPEC_MFSA:
        if (inst->rd == 0) inst->flags.modifies_gpr = false;
        break;
    case SPEC_MTSA:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case SPEC_SYNC:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    default:
        break;
    }
}

static void decode_regimm(r5900_inst_t *inst)
{
    uint32_t rt = inst->rt;
    switch (rt) {
    case REGIMM_BLTZ:  case REGIMM_BGEZ:
    case REGIMM_BLTZL: case REGIMM_BGEZL:
        inst->flags.is_branch = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_control = true;
        break;

    case REGIMM_BLTZAL:  case REGIMM_BGEZAL:
    case REGIMM_BLTZALL: case REGIMM_BGEZALL:
        inst->flags.is_branch = true;
        inst->flags.is_call = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_gpr = true;    /* $ra */
        inst->flags.modifies_control = true;
        break;

    case REGIMM_TGEI:  case REGIMM_TGEIU:
    case REGIMM_TLTI:  case REGIMM_TLTIU:
    case REGIMM_TEQI:  case REGIMM_TNEI:
        inst->flags.modifies_control = true;
        break;

    case REGIMM_MTSAB: case REGIMM_MTSAH:
        inst->flags.is_multimedia = true;
        inst->flags.modifies_control = true;
        break;

    default:
        break;
    }
}

/* Forward declarations for MMI sub-decoders */
static void decode_mmi0(r5900_inst_t *inst);
static void decode_mmi1(r5900_inst_t *inst);
static void decode_mmi2(r5900_inst_t *inst);
static void decode_mmi3(r5900_inst_t *inst);
static void decode_pmfhl(r5900_inst_t *inst);

static void decode_mmi(r5900_inst_t *inst)
{
    inst->flags.is_mmi = true;
    inst->flags.is_multimedia = true;
    inst->flags.modifies_gpr = (inst->rd != 0);

    uint32_t func = inst->function;

    if (func == MMI_MMI0)  { decode_mmi0(inst); return; }
    if (func == MMI_MMI2)  { decode_mmi2(inst); return; }
    if (func == MMI_MMI1)  { decode_mmi1(inst); return; }
    if (func == MMI_MMI3)  { decode_mmi3(inst); return; }

    switch (func) {
    case MMI_PLZCW:
        break;

    case MMI_MFHI1: case MMI_MFLO1:
        if (inst->rd == 0) inst->flags.modifies_gpr = false;
        break;

    case MMI_MTHI1: case MMI_MTLO1:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case MMI_PSLLH: case MMI_PSRLH: case MMI_PSRAH:
    case MMI_PSLLW: case MMI_PSRLW: case MMI_PSRAW:
        break;

    case MMI_MADD:  case MMI_MADDU:
    case MMI_MSUB:  case MMI_MSUBU:
    case MMI_MADD1: case MMI_MADDU1:
    case MMI_MULT1: case MMI_MULTU1:
    case MMI_DIV1:  case MMI_DIVU1:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case MMI_PMTHL:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;

    case MMI_PMFHL:
        decode_pmfhl(inst);
        break;

    default:
        break;
    }
}

static void decode_mmi0(r5900_inst_t *inst)
{
    inst->mmi_type = 0;
    inst->mmi_function = (uint8_t)inst->sa;
    /* All MMI0 ops: PADDW, PSUBW, PCGTW, PMAXW, PADDH, PSUBH, PCGTH,
       PMAXH, PADDB, PSUBB, PCGTB, PADDSW, PSUBSW, PEXTLW, PPACW,
       PADDSH, PSUBSH, PEXTLH, PPACH, PADDSB, PSUBSB, PEXTLB, PPACB,
       PEXT5, PPAC5
       Default: modifies GPR (rd) — already set by decode_mmi */
}

static void decode_mmi1(r5900_inst_t *inst)
{
    inst->mmi_type = 1;
    inst->mmi_function = (uint8_t)inst->sa;
    /* MMI1: PABSW, PCEQW, PMINW, PADSBH, PABSH, PCEQH, PMINH,
       PCEQB, PADDUW, PSUBUW, PEXTUW, PADDUH, PSUBUH, PEXTUH,
       PADDUB, PSUBUB, PEXTUB, QFSRV */
    if (inst->sa == MMI1_QFSRV)
        inst->flags.is_multimedia = true;
}

static void decode_mmi2(r5900_inst_t *inst)
{
    inst->mmi_type = 2;
    inst->mmi_function = (uint8_t)inst->sa;

    uint32_t sa = inst->sa;
    /* Some MMI2 ops modify HI/LO */
    if (sa == MMI2_PMADDW || sa == MMI2_PMSUBW ||
        sa == MMI2_PMULTW || sa == MMI2_PDIVW  || sa == MMI2_PDIVBW ||
        sa == MMI2_PMADDH || sa == MMI2_PHMADH ||
        sa == MMI2_PMSUBH || sa == MMI2_PHMSBH || sa == MMI2_PMULTH) {
        inst->flags.modifies_control = true;
    }
}

static void decode_mmi3(r5900_inst_t *inst)
{
    inst->mmi_type = 3;
    inst->mmi_function = (uint8_t)inst->sa;

    uint32_t sa = inst->sa;
    switch (sa) {
    case MMI3_PMADDUW:
    case MMI3_PMULTUW:
    case MMI3_PDIVUW:
        inst->flags.is_multimedia = true;
        inst->flags.modifies_control = true;
        break;
    case MMI3_PMTHI:
    case MMI3_PMTLO:
        inst->flags.modifies_gpr = false;
        inst->flags.modifies_control = true;
        break;
    default:
        break;
    }
}

static void decode_pmfhl(r5900_inst_t *inst)
{
    uint32_t sa = inst->sa;
    switch (sa) {
    case PMFHL_LW:  inst->pmfhl_var = PMFHL_LW;  break;
    case PMFHL_UW:  inst->pmfhl_var = PMFHL_UW;  break;
    case PMFHL_SLW: inst->pmfhl_var = PMFHL_SLW; break;
    case PMFHL_LH:  inst->pmfhl_var = PMFHL_LH;  break;
    case PMFHL_SH:  inst->pmfhl_var = PMFHL_SH;  break;
    default:        inst->pmfhl_var = 0xFF;       break;
    }
}

static void decode_cop0(r5900_inst_t *inst)
{
    uint8_t fmt = (uint8_t)inst->rs;
    inst->flags.modifies_control = true;

    if (fmt == COP0_MF && inst->rt != 0)
        inst->flags.modifies_gpr = true;
    else if (fmt == COP0_CO && inst->function == COP0_ERET) {
        inst->flags.is_return = true;
        inst->flags.has_delay_slot = false; /* ERET has NO delay slot */
    }
    else if (fmt == COP0_BC) {
        inst->flags.is_branch = true;
        inst->flags.has_delay_slot = true;
    }
}

static void decode_cop1(r5900_inst_t *inst)
{
    uint8_t fmt = (uint8_t)inst->rs;

    if (fmt == COP1_MF || fmt == COP1_CF) {
        if (inst->rt != 0)
            inst->flags.modifies_gpr = true;
    }
    else if (fmt == COP1_BC) {
        inst->flags.is_branch = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_control = true;
    }
    else if (fmt == COP1_S || fmt == COP1_W) {
        inst->flags.modifies_fpr = true;
    }

    if (fmt == COP1_CT || (fmt == COP1_S && inst->function >= COP1S_C_F))
        inst->flags.modifies_control = true; /* FCR31 */
}

static void decode_cop2(r5900_inst_t *inst)
{
    uint8_t fmt = (uint8_t)inst->rs;
    inst->flags.is_vu = true;
    inst->flags.is_multimedia = true;

    switch (fmt) {
    case COP2_QMFC2:
    case COP2_CFC2:
        if (inst->rt != 0)
            inst->flags.modifies_gpr = true;
        break;
    case COP2_QMTC2:
        inst->flags.modifies_vfr = true;
        break;
    case COP2_CTC2:
        inst->flags.modifies_control = true;
        break;
    case COP2_BC:
        inst->flags.is_branch = true;
        inst->flags.has_delay_slot = true;
        inst->flags.modifies_control = true;
        break;
    default:
        /* COP2_CO and variants (format >= 0x10) — VU0 macro ops */
        if (fmt >= COP2_CO) {
            inst->vu_dest = R5900_VU_DEST(inst->raw);
            inst->flags.modifies_vfr = true;
            inst->flags.modifies_control = true;

            uint8_t vu_func = (uint8_t)inst->function;

            /* Extract fsf/ftf for division instructions */
            if (vu_func == VU0S2_VDIV || vu_func == VU0S2_VSQRT ||
                vu_func == VU0S2_VRSQRT) {
                inst->vu_fsf = R5900_VU_FSF(inst->raw);
                inst->vu_ftf = R5900_VU_FTF(inst->raw);
            }

            /* Refine flags for special VU ops */
            if (vu_func >= 0x3C) {
                /* Special2 table */
                switch (vu_func) {
                case VU0S2_VMTIR:
                    inst->flags.modifies_vfr = false;
                    inst->flags.modifies_vir = true;
                    break;
                case VU0S2_VILWR:
                    inst->flags.modifies_vfr = false;
                    inst->flags.modifies_vir = true;
                    inst->flags.is_load = true;
                    break;
                case VU0S2_VISWR:
                    inst->flags.modifies_vfr = false;
                    inst->flags.is_store = true;
                    inst->flags.modifies_memory = true;
                    break;
                case VU0S2_VNOP:
                    inst->flags.modifies_vfr = false;
                    inst->flags.modifies_control = false;
                    break;
                default:
                    break;
                }
            } else {
                /* Special1 table */
                if (vu_func >= VU0S1_VIADD && vu_func <= VU0S1_VIOR) {
                    inst->flags.modifies_vfr = false;
                    inst->flags.modifies_vir = true;
                }
                if (vu_func == VU0S1_VIADDI) {
                    inst->flags.modifies_vfr = false;
                    inst->flags.modifies_vir = true;
                }
            }
        }
        break;
    }
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

void r5900_decode(uint32_t address, uint32_t raw, r5900_inst_t *out)
{
    memset(out, 0, sizeof(*out));

    out->address    = address;
    out->raw        = raw;
    out->opcode     = R5900_OPCODE(raw);
    out->rs         = R5900_RS(raw);
    out->rt         = R5900_RT(raw);
    out->rd         = R5900_RD(raw);
    out->sa         = R5900_SA(raw);
    out->function   = R5900_FUNCTION(raw);
    out->immediate  = R5900_IMMEDIATE(raw);
    out->simmediate = R5900_SIMMEDIATE(raw);
    out->target     = R5900_TARGET(raw);

    /* Default VU dest field (all xyzw) */
    out->vu_dest = 0xF;

    switch (out->opcode) {
    case OP_SPECIAL: decode_special(out); break;
    case OP_REGIMM:  decode_regimm(out);  break;

    case OP_J:
    case OP_JAL:
        out->flags.is_jump = true;
        out->flags.has_delay_slot = true;
        out->flags.modifies_control = true;
        if (out->opcode == OP_JAL)
            out->flags.is_call = true;
        break;

    case OP_BEQ:  case OP_BNE:
    case OP_BLEZ: case OP_BGTZ:
    case OP_BEQL: case OP_BNEL:
    case OP_BLEZL:case OP_BGTZL:
        out->flags.is_branch = true;
        out->flags.has_delay_slot = true;
        out->flags.modifies_control = true;
        break;

    case OP_DADDI: case OP_DADDIU:
        if (out->rt != 0) out->flags.modifies_gpr = true;
        break;

    case OP_MMI: decode_mmi(out); break;

    case OP_LQ:
        out->flags.is_load = true;
        out->flags.is_multimedia = true;
        break;
    case OP_SQ:
        out->flags.is_store = true;
        out->flags.is_multimedia = true;
        out->flags.modifies_memory = true;
        break;

    case OP_LB:  case OP_LH:  case OP_LW:
    case OP_LBU: case OP_LHU: case OP_LWU: case OP_LD:
        out->flags.is_load = true;
        if (out->rt != 0) out->flags.modifies_gpr = true;
        break;

    case OP_LWL: case OP_LWR: case OP_LDL: case OP_LDR:
        out->flags.is_load = true;
        if (out->rt != 0) out->flags.modifies_gpr = true;
        break;

    case OP_LL: case OP_LLD:
        out->flags.is_load = true;
        if (out->rt != 0) out->flags.modifies_gpr = true;
        out->flags.modifies_control = true; /* LLBit */
        break;

    case OP_LWC1:
        out->flags.is_load = true;
        out->flags.modifies_fpr = true;
        break;
    case OP_LDC1: case OP_LWC2: case OP_LDC2:
        out->flags.is_load = true;
        out->flags.is_vu = true;
        out->flags.modifies_vfr = true;
        break;

    case OP_SB: case OP_SH: case OP_SW: case OP_SD:
    case OP_SWL: case OP_SWR: case OP_SDL: case OP_SDR:
        out->flags.is_store = true;
        out->flags.modifies_memory = true;
        break;

    case OP_SWC1:
        out->flags.is_store = true;
        out->flags.modifies_memory = true;
        break;
    case OP_SDC1: case OP_SWC2: case OP_SDC2:
        out->flags.is_store = true;
        out->flags.is_vu = true;
        out->flags.modifies_memory = true;
        break;

    case OP_SC: case OP_SCD:
        out->flags.is_store = true;
        out->flags.modifies_memory = true;
        if (out->rt != 0) out->flags.modifies_gpr = true;
        out->flags.modifies_control = true;
        break;

    case OP_ADDI: case OP_ADDIU:
    case OP_SLTI: case OP_SLTIU:
    case OP_ANDI: case OP_ORI: case OP_XORI:
    case OP_LUI:
        if (out->rt != 0) out->flags.modifies_gpr = true;
        break;

    case OP_CACHE:
        out->flags.modifies_control = true;
        break;

    case OP_PREF:
        break;

    case OP_COP0: decode_cop0(out); break;
    case OP_COP1: decode_cop1(out); break;
    case OP_COP2: decode_cop2(out); break;

    default:
        break;
    }

    /* Generic multimedia flag */
    if (out->flags.is_mmi || out->flags.is_vu)
        out->flags.is_multimedia = true;
}

uint32_t r5900_branch_target(const r5900_inst_t *inst)
{
    if (!inst->flags.is_branch)
        return 0;
    int32_t offset = inst->simmediate << 2;
    return inst->address + 4 + (uint32_t)offset;
}

uint32_t r5900_jump_target(const r5900_inst_t *inst)
{
    if (!inst->flags.is_jump)
        return 0;

    /* JR/JALR: target is in rs register — can't determine statically */
    if (inst->opcode == OP_SPECIAL &&
        (inst->function == SPEC_JR || inst->function == SPEC_JALR))
        return 0;

    /* J/JAL: combine upper 4 bits of PC+4 with 26-bit target << 2 */
    if (inst->opcode == OP_J || inst->opcode == OP_JAL) {
        uint32_t pc_upper = (inst->address + 4) & 0xF0000000;
        return pc_upper | (inst->target << 2);
    }

    return 0;
}

/* =========================================================================
 * Disassembler — mnemonic generation
 * ========================================================================= */

/** Helper: write opcode + register operands for R-type ALU */
static int disasm_r_alu(const r5900_inst_t *inst, const char *mnemonic,
                        char *buf, size_t sz)
{
    return snprintf(buf, sz, "%-8s %s, %s, %s",
                    mnemonic, gpr_names[inst->rd],
                    gpr_names[inst->rs], gpr_names[inst->rt]);
}

/** Helper for shift by sa */
static int disasm_r_shift(const r5900_inst_t *inst, const char *mnemonic,
                          char *buf, size_t sz)
{
    return snprintf(buf, sz, "%-8s %s, %s, %u",
                    mnemonic, gpr_names[inst->rd],
                    gpr_names[inst->rt], inst->sa);
}

/** Helper for I-type rt, rs, imm */
static int disasm_i_alu(const r5900_inst_t *inst, const char *mnemonic,
                        bool sign_ext, char *buf, size_t sz)
{
    if (sign_ext)
        return snprintf(buf, sz, "%-8s %s, %s, %d",
                        mnemonic, gpr_names[inst->rt],
                        gpr_names[inst->rs], inst->simmediate);
    else
        return snprintf(buf, sz, "%-8s %s, %s, 0x%04X",
                        mnemonic, gpr_names[inst->rt],
                        gpr_names[inst->rs], inst->immediate);
}

/** Helper for load/store: rt, offset(base) */
static int disasm_loadstore(const r5900_inst_t *inst, const char *mnemonic,
                            char *buf, size_t sz)
{
    return snprintf(buf, sz, "%-8s %s, %d(%s)",
                    mnemonic, gpr_names[inst->rt],
                    inst->simmediate, gpr_names[inst->rs]);
}

/** Helper for branch: rs, rt, target */
static int disasm_branch2(const r5900_inst_t *inst, const char *mnemonic,
                          char *buf, size_t sz)
{
    uint32_t target = r5900_branch_target(inst);
    return snprintf(buf, sz, "%-8s %s, %s, 0x%08X",
                    mnemonic, gpr_names[inst->rs],
                    gpr_names[inst->rt], target);
}

/** Helper for branch: rs, target */
static int disasm_branch1(const r5900_inst_t *inst, const char *mnemonic,
                          char *buf, size_t sz)
{
    uint32_t target = r5900_branch_target(inst);
    return snprintf(buf, sz, "%-8s %s, 0x%08X",
                    mnemonic, gpr_names[inst->rs], target);
}

static int disasm_special(const r5900_inst_t *inst, char *buf, size_t sz)
{
    switch (inst->function) {
    case SPEC_SLL:
        if (inst->raw == 0) return snprintf(buf, sz, "NOP");
        return disasm_r_shift(inst, "SLL", buf, sz);
    case SPEC_SRL:    return disasm_r_shift(inst, "SRL", buf, sz);
    case SPEC_SRA:    return disasm_r_shift(inst, "SRA", buf, sz);
    case SPEC_SLLV:   return disasm_r_alu(inst, "SLLV", buf, sz);
    case SPEC_SRLV:   return disasm_r_alu(inst, "SRLV", buf, sz);
    case SPEC_SRAV:   return disasm_r_alu(inst, "SRAV", buf, sz);
    case SPEC_JR:     return snprintf(buf, sz, "%-8s %s", "JR", gpr_names[inst->rs]);
    case SPEC_JALR:   return snprintf(buf, sz, "%-8s %s, %s", "JALR", gpr_names[inst->rd], gpr_names[inst->rs]);
    case SPEC_SYSCALL:return snprintf(buf, sz, "SYSCALL");
    case SPEC_BREAK:  return snprintf(buf, sz, "BREAK");
    case SPEC_SYNC:   return snprintf(buf, sz, "SYNC");
    case SPEC_MFHI:   return snprintf(buf, sz, "%-8s %s", "MFHI", gpr_names[inst->rd]);
    case SPEC_MTHI:   return snprintf(buf, sz, "%-8s %s", "MTHI", gpr_names[inst->rs]);
    case SPEC_MFLO:   return snprintf(buf, sz, "%-8s %s", "MFLO", gpr_names[inst->rd]);
    case SPEC_MTLO:   return snprintf(buf, sz, "%-8s %s", "MTLO", gpr_names[inst->rs]);
    case SPEC_MULT:   return snprintf(buf, sz, "%-8s %s, %s", "MULT", gpr_names[inst->rs], gpr_names[inst->rt]);
    case SPEC_MULTU:  return snprintf(buf, sz, "%-8s %s, %s", "MULTU", gpr_names[inst->rs], gpr_names[inst->rt]);
    case SPEC_DIV:    return snprintf(buf, sz, "%-8s %s, %s", "DIV", gpr_names[inst->rs], gpr_names[inst->rt]);
    case SPEC_DIVU:   return snprintf(buf, sz, "%-8s %s, %s", "DIVU", gpr_names[inst->rs], gpr_names[inst->rt]);
    case SPEC_ADD:    return disasm_r_alu(inst, "ADD", buf, sz);
    case SPEC_ADDU:   return disasm_r_alu(inst, "ADDU", buf, sz);
    case SPEC_SUB:    return disasm_r_alu(inst, "SUB", buf, sz);
    case SPEC_SUBU:   return disasm_r_alu(inst, "SUBU", buf, sz);
    case SPEC_AND:    return disasm_r_alu(inst, "AND", buf, sz);
    case SPEC_OR:     return disasm_r_alu(inst, "OR", buf, sz);
    case SPEC_XOR:    return disasm_r_alu(inst, "XOR", buf, sz);
    case SPEC_NOR:    return disasm_r_alu(inst, "NOR", buf, sz);
    case SPEC_SLT:    return disasm_r_alu(inst, "SLT", buf, sz);
    case SPEC_SLTU:   return disasm_r_alu(inst, "SLTU", buf, sz);
    case SPEC_MOVZ:   return disasm_r_alu(inst, "MOVZ", buf, sz);
    case SPEC_MOVN:   return disasm_r_alu(inst, "MOVN", buf, sz);
    case SPEC_MFSA:   return snprintf(buf, sz, "%-8s %s", "MFSA", gpr_names[inst->rd]);
    case SPEC_MTSA:   return snprintf(buf, sz, "%-8s %s", "MTSA", gpr_names[inst->rs]);
    case SPEC_DADD:   return disasm_r_alu(inst, "DADD", buf, sz);
    case SPEC_DADDU:  return disasm_r_alu(inst, "DADDU", buf, sz);
    case SPEC_DSUB:   return disasm_r_alu(inst, "DSUB", buf, sz);
    case SPEC_DSUBU:  return disasm_r_alu(inst, "DSUBU", buf, sz);
    case SPEC_DSLL:   return disasm_r_shift(inst, "DSLL", buf, sz);
    case SPEC_DSRL:   return disasm_r_shift(inst, "DSRL", buf, sz);
    case SPEC_DSRA:   return disasm_r_shift(inst, "DSRA", buf, sz);
    case SPEC_DSLL32: return disasm_r_shift(inst, "DSLL32", buf, sz);
    case SPEC_DSRL32: return disasm_r_shift(inst, "DSRL32", buf, sz);
    case SPEC_DSRA32: return disasm_r_shift(inst, "DSRA32", buf, sz);
    case SPEC_DSLLV:  return disasm_r_alu(inst, "DSLLV", buf, sz);
    case SPEC_DSRLV:  return disasm_r_alu(inst, "DSRLV", buf, sz);
    case SPEC_DSRAV:  return disasm_r_alu(inst, "DSRAV", buf, sz);
    case SPEC_TGE:    return disasm_r_alu(inst, "TGE", buf, sz);
    case SPEC_TGEU:   return disasm_r_alu(inst, "TGEU", buf, sz);
    case SPEC_TLT:    return disasm_r_alu(inst, "TLT", buf, sz);
    case SPEC_TLTU:   return disasm_r_alu(inst, "TLTU", buf, sz);
    case SPEC_TEQ:    return disasm_r_alu(inst, "TEQ", buf, sz);
    case SPEC_TNE:    return disasm_r_alu(inst, "TNE", buf, sz);
    default:
        return snprintf(buf, sz, "SPECIAL_0x%02X", inst->function);
    }
}

static int disasm_regimm(const r5900_inst_t *inst, char *buf, size_t sz)
{
    switch (inst->rt) {
    case REGIMM_BLTZ:    return disasm_branch1(inst, "BLTZ", buf, sz);
    case REGIMM_BGEZ:    return disasm_branch1(inst, "BGEZ", buf, sz);
    case REGIMM_BLTZL:   return disasm_branch1(inst, "BLTZL", buf, sz);
    case REGIMM_BGEZL:   return disasm_branch1(inst, "BGEZL", buf, sz);
    case REGIMM_BLTZAL:  return disasm_branch1(inst, "BLTZAL", buf, sz);
    case REGIMM_BGEZAL:  return disasm_branch1(inst, "BGEZAL", buf, sz);
    case REGIMM_BLTZALL: return disasm_branch1(inst, "BLTZALL", buf, sz);
    case REGIMM_BGEZALL: return disasm_branch1(inst, "BGEZALL", buf, sz);
    case REGIMM_MTSAB:   return snprintf(buf, sz, "%-8s %s, 0x%04X", "MTSAB", gpr_names[inst->rs], inst->immediate);
    case REGIMM_MTSAH:   return snprintf(buf, sz, "%-8s %s, 0x%04X", "MTSAH", gpr_names[inst->rs], inst->immediate);
    case REGIMM_TGEI:    return disasm_i_alu(inst, "TGEI", true, buf, sz);
    case REGIMM_TGEIU:   return disasm_i_alu(inst, "TGEIU", true, buf, sz);
    case REGIMM_TLTI:    return disasm_i_alu(inst, "TLTI", true, buf, sz);
    case REGIMM_TLTIU:   return disasm_i_alu(inst, "TLTIU", true, buf, sz);
    case REGIMM_TEQI:    return disasm_i_alu(inst, "TEQI", true, buf, sz);
    case REGIMM_TNEI:    return disasm_i_alu(inst, "TNEI", true, buf, sz);
    default:
        return snprintf(buf, sz, "REGIMM_0x%02X", inst->rt);
    }
}

int r5900_disasm(const r5900_inst_t *inst, char *buf, size_t buf_size)
{
    if (!inst || !buf || buf_size == 0) return 0;

    switch (inst->opcode) {
    case OP_SPECIAL: return disasm_special(inst, buf, buf_size);
    case OP_REGIMM:  return disasm_regimm(inst, buf, buf_size);

    case OP_J:     { uint32_t t = r5900_jump_target(inst); return snprintf(buf, buf_size, "%-8s 0x%08X", "J", t); }
    case OP_JAL:   { uint32_t t = r5900_jump_target(inst); return snprintf(buf, buf_size, "%-8s 0x%08X", "JAL", t); }

    case OP_BEQ:   return disasm_branch2(inst, "BEQ", buf, buf_size);
    case OP_BNE:   return disasm_branch2(inst, "BNE", buf, buf_size);
    case OP_BLEZ:  return disasm_branch1(inst, "BLEZ", buf, buf_size);
    case OP_BGTZ:  return disasm_branch1(inst, "BGTZ", buf, buf_size);
    case OP_BEQL:  return disasm_branch2(inst, "BEQL", buf, buf_size);
    case OP_BNEL:  return disasm_branch2(inst, "BNEL", buf, buf_size);
    case OP_BLEZL: return disasm_branch1(inst, "BLEZL", buf, buf_size);
    case OP_BGTZL: return disasm_branch1(inst, "BGTZL", buf, buf_size);

    case OP_ADDI:  return disasm_i_alu(inst, "ADDI", true, buf, buf_size);
    case OP_ADDIU: return disasm_i_alu(inst, "ADDIU", true, buf, buf_size);
    case OP_SLTI:  return disasm_i_alu(inst, "SLTI", true, buf, buf_size);
    case OP_SLTIU: return disasm_i_alu(inst, "SLTIU", false, buf, buf_size);
    case OP_ANDI:  return disasm_i_alu(inst, "ANDI", false, buf, buf_size);
    case OP_ORI:   return disasm_i_alu(inst, "ORI", false, buf, buf_size);
    case OP_XORI:  return disasm_i_alu(inst, "XORI", false, buf, buf_size);
    case OP_LUI:
        return snprintf(buf, buf_size, "%-8s %s, 0x%04X", "LUI",
                        gpr_names[inst->rt], inst->immediate);

    case OP_DADDI:  return disasm_i_alu(inst, "DADDI", true, buf, buf_size);
    case OP_DADDIU: return disasm_i_alu(inst, "DADDIU", true, buf, buf_size);

    /* Load/Store */
    case OP_LB:   return disasm_loadstore(inst, "LB", buf, buf_size);
    case OP_LH:   return disasm_loadstore(inst, "LH", buf, buf_size);
    case OP_LW:   return disasm_loadstore(inst, "LW", buf, buf_size);
    case OP_LBU:  return disasm_loadstore(inst, "LBU", buf, buf_size);
    case OP_LHU:  return disasm_loadstore(inst, "LHU", buf, buf_size);
    case OP_LWU:  return disasm_loadstore(inst, "LWU", buf, buf_size);
    case OP_LD:   return disasm_loadstore(inst, "LD", buf, buf_size);
    case OP_LWL:  return disasm_loadstore(inst, "LWL", buf, buf_size);
    case OP_LWR:  return disasm_loadstore(inst, "LWR", buf, buf_size);
    case OP_LDL:  return disasm_loadstore(inst, "LDL", buf, buf_size);
    case OP_LDR:  return disasm_loadstore(inst, "LDR", buf, buf_size);
    case OP_LQ:   return disasm_loadstore(inst, "LQ", buf, buf_size);
    case OP_LL:   return disasm_loadstore(inst, "LL", buf, buf_size);
    case OP_LLD:  return disasm_loadstore(inst, "LLD", buf, buf_size);
    case OP_LWC1: return disasm_loadstore(inst, "LWC1", buf, buf_size);
    case OP_LDC2: return disasm_loadstore(inst, "LQC2", buf, buf_size);

    case OP_SB:   return disasm_loadstore(inst, "SB", buf, buf_size);
    case OP_SH:   return disasm_loadstore(inst, "SH", buf, buf_size);
    case OP_SW:   return disasm_loadstore(inst, "SW", buf, buf_size);
    case OP_SD:   return disasm_loadstore(inst, "SD", buf, buf_size);
    case OP_SWL:  return disasm_loadstore(inst, "SWL", buf, buf_size);
    case OP_SWR:  return disasm_loadstore(inst, "SWR", buf, buf_size);
    case OP_SDL:  return disasm_loadstore(inst, "SDL", buf, buf_size);
    case OP_SDR:  return disasm_loadstore(inst, "SDR", buf, buf_size);
    case OP_SQ:   return disasm_loadstore(inst, "SQ", buf, buf_size);
    case OP_SC:   return disasm_loadstore(inst, "SC", buf, buf_size);
    case OP_SCD:  return disasm_loadstore(inst, "SCD", buf, buf_size);
    case OP_SWC1: return disasm_loadstore(inst, "SWC1", buf, buf_size);
    case OP_SDC2: return disasm_loadstore(inst, "SQC2", buf, buf_size);

    case OP_CACHE: return snprintf(buf, buf_size, "CACHE");
    case OP_PREF:  return snprintf(buf, buf_size, "PREF");

    /* COP0/COP1/COP2 — simplified mnemonics */
    case OP_COP0:
        return snprintf(buf, buf_size, "COP0    fmt=0x%02X func=0x%02X",
                        inst->rs, inst->function);
    case OP_COP1:
        return snprintf(buf, buf_size, "COP1    fmt=0x%02X func=0x%02X",
                        inst->rs, inst->function);
    case OP_COP2:
        return snprintf(buf, buf_size, "COP2    fmt=0x%02X func=0x%02X",
                        inst->rs, inst->function);

    /* MMI */
    case OP_MMI:
        return snprintf(buf, buf_size, "MMI     func=0x%02X sa=0x%02X",
                        inst->function, inst->sa);

    default:
        return snprintf(buf, buf_size, "??? (opcode=0x%02X)", inst->opcode);
    }
}

/* =========================================================================
 * Buffer decode + file output
 * ========================================================================= */

void r5900_decode_buffer(const uint8_t *code, size_t code_size,
                         uint32_t base_addr,
                         r5900_inst_t *out, size_t *out_count)
{
    size_t count = code_size / 4;
    for (size_t i = 0; i < count; i++) {
        /* MIPS is little-endian on PS2 */
        uint32_t raw = (uint32_t)code[i*4 + 0]
                     | ((uint32_t)code[i*4 + 1] << 8)
                     | ((uint32_t)code[i*4 + 2] << 16)
                     | ((uint32_t)code[i*4 + 3] << 24);
        r5900_decode(base_addr + (uint32_t)(i * 4), raw, &out[i]);
    }
    if (out_count)
        *out_count = count;
}

void r5900_disasm_to_file(const uint8_t *code, size_t code_size,
                          uint32_t base_addr, const char *output_path)
{
    if (!code || code_size < 4 || !output_path) {
        fprintf(stderr, "r5900_disasm_to_file: invalid parameters\n");
        return;
    }

    FILE *f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "Error opening output file: %s\n", output_path);
        return;
    }

    size_t count = code_size / 4;
    r5900_inst_t inst;
    char line[256];

    for (size_t i = 0; i < count; i++) {
        uint32_t raw = (uint32_t)code[i*4 + 0]
                     | ((uint32_t)code[i*4 + 1] << 8)
                     | ((uint32_t)code[i*4 + 2] << 16)
                     | ((uint32_t)code[i*4 + 3] << 24);

        r5900_decode(base_addr + (uint32_t)(i * 4), raw, &inst);
        r5900_disasm(&inst, line, sizeof(line));

        fprintf(f, "0x%08X:  %08X  %s\n", inst.address, inst.raw, line);
    }

    fclose(f);
    printf("R5900 disassembly saved to: %s (%zu instructions)\n",
           output_path, count);
}

/* =========================================================================
 * Legacy reference: original disasm.c (Capstone-based x86/MIPS)
 *
 * Kept here commented out for study. The new implementation above uses
 * the native R5900 decoder — no external dependencies.
 *
 * #include <capstone/capstone.h>
 *
 * void disassemble_code(const uint8_t *code, size_t code_size, const char *name) {
 *     csh handle;
 *     cs_err err = cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN, &handle);
 *     ...
 *     cs_insn *insn;
 *     size_t count = cs_disasm(handle, code, code_size, 0x1000, 0, &insn);
 *     for (size_t i = 0; i < count; i++) {
 *         fprintf(log_file, "0x%08" PRIx64 ":\t%s\t%s\n",
 *                 insn[i].address, insn[i].mnemonic, insn[i].op_str);
 *     }
 *     cs_free(insn, count);
 *     cs_close(&handle);
 * }
 * ========================================================================= */
