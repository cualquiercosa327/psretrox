/**
 * @file r5900_decompiler.h
 * @brief R5900 (EE core) instruction decoder and decompiler — pure C port.
 *
 * Migrated from ps2recomp's r5900_decoder.h / instructions.h / types.h (C++)
 * and expanded with additional references:
 *
 * References:
 *   - "See MIPS Run" (Dominic Sweetman) — MIPS architecture deep-dive
 *   - ps2tek  https://psi-rockin.github.io/ps2tek/  — PS2 hardware reference
 *   - Rodrigo Copetti "PS2 Architecture"
 *         https://www.copetti.org/writings/consoles/playstation-2/
 *   - ELF Spec: https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html
 *   - PS2 Programming Docs: https://github.com/DarrenRainey/PS2-Programming-Docs/
 *   - ECMA-119 5th edition (ISO 9660)
 *
 * The R5900 is the Emotion Engine (EE) main CPU of the PS2.
 * It is a MIPS III/IV derivative with 128-bit GPRs and custom extensions:
 *   - 128-bit registers (used by LQ/SQ, MMI instructions)
 *   - Multimedia Instructions (MMI) — SIMD on 128-bit GPRs
 *   - VU0 macro mode (COP2) — vector/float SIMD
 *   - Dual pipeline HI/LO (HI1/LO1 for pipeline 1)
 *   - SA register for funnel shifts (QFSRV, MTSAB, MTSAH)
 *
 * Register conventions (MIPS n32/n64 adapted):
 *   $0  ($zero) — always 0
 *   $1  ($at)   — assembler temporary
 *   $2-$3  ($v0-$v1) — return values
 *   $4-$7  ($a0-$a3) — arguments
 *   $8-$15 ($t0-$t7) — temporaries
 *   $16-$23 ($s0-$s7) — saved
 *   $24-$25 ($t8-$t9) — temporaries
 *   $26-$27 ($k0-$k1) — kernel
 *   $28 ($gp)  — global pointer
 *   $29 ($sp)  — stack pointer
 *   $30 ($fp)  — frame pointer
 *   $31 ($ra)  — return address
 */

#ifndef R5900_DECOMPILER_H
#define R5900_DECOMPILER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Instruction field extraction macros
 * ========================================================================= */

/** Primary opcode: bits [31:26] */
#define R5900_OPCODE(inst)      (((inst) >> 26) & 0x3F)
/** Source register (rs): bits [25:21] */
#define R5900_RS(inst)          (((inst) >> 21) & 0x1F)
/** Target register (rt): bits [20:16] */
#define R5900_RT(inst)          (((inst) >> 16) & 0x1F)
/** Destination register (rd): bits [15:11] */
#define R5900_RD(inst)          (((inst) >> 11) & 0x1F)
/** Shift amount (sa): bits [10:6] */
#define R5900_SA(inst)          (((inst) >> 6) & 0x1F)
/** Function field: bits [5:0] */
#define R5900_FUNCTION(inst)    ((inst) & 0x3F)
/** Unsigned immediate (16-bit): bits [15:0] */
#define R5900_IMMEDIATE(inst)   ((inst) & 0xFFFF)
/** Signed immediate (16-bit, sign-extended to 32-bit) */
#define R5900_SIMMEDIATE(inst)  ((int32_t)(int16_t)((inst) & 0xFFFF))
/** Jump target (26-bit): bits [25:0] */
#define R5900_TARGET(inst)      ((inst) & 0x03FFFFFF)

/* COP function field (same as FUNCTION) */
#define R5900_COP_FUNCT(inst)   ((uint8_t)((inst) & 0x3F))
/** FPU format field (rs): bits [25:21] */
#define R5900_FPU_FMT(inst)     ((uint8_t)(((inst) >> 21) & 0x1F))
/** FPU ft register (same as RT) */
#define R5900_FT(inst)          R5900_RT(inst)
/** FPU fs register (same as RD) */
#define R5900_FS(inst)          R5900_RD(inst)
/** FPU fd register (same as SA) */
#define R5900_FD(inst)          R5900_SA(inst)

/* VU macro field extractions */
#define R5900_VU_DEST(inst)     ((uint8_t)(((inst) >> 21) & 0xF))
#define R5900_VU_FSF(inst)      ((uint8_t)(((inst) >> 10) & 0x3))
#define R5900_VU_FTF(inst)      ((uint8_t)(((inst) >> 8) & 0x3))
#define R5900_VU_IS(inst)       R5900_RT(inst)
#define R5900_VU_IT(inst)       R5900_RD(inst)
#define R5900_VU_ID(inst)       R5900_SA(inst)

/* =========================================================================
 * Primary opcodes — bits [31:26]
 *
 * Standard MIPS + PS2 EE extensions (MMI, LQ, SQ)
 * See: "See MIPS Run" Chapter 9; ps2tek EE section
 * ========================================================================= */

enum r5900_opcode {
    /* Standard MIPS */
    OP_SPECIAL  = 0x00,  /* R-type sub-group (see r5900_special) */
    OP_REGIMM   = 0x01,  /* REGIMM sub-group (see r5900_regimm) */
    OP_J        = 0x02,  /* Jump */
    OP_JAL      = 0x03,  /* Jump And Link */
    OP_BEQ      = 0x04,  /* Branch on Equal */
    OP_BNE      = 0x05,  /* Branch on Not Equal */
    OP_BLEZ     = 0x06,  /* Branch on <= 0 */
    OP_BGTZ     = 0x07,  /* Branch on > 0 */

    OP_ADDI     = 0x08,  /* Add Immediate (with overflow) */
    OP_ADDIU    = 0x09,  /* Add Immediate Unsigned */
    OP_SLTI     = 0x0A,  /* Set on Less Than Immediate */
    OP_SLTIU    = 0x0B,  /* Set on Less Than Immediate Unsigned */
    OP_ANDI     = 0x0C,  /* AND Immediate */
    OP_ORI      = 0x0D,  /* OR Immediate */
    OP_XORI     = 0x0E,  /* XOR Immediate */
    OP_LUI      = 0x0F,  /* Load Upper Immediate */

    OP_COP0     = 0x10,  /* Coprocessor 0 (System Control) */
    OP_COP1     = 0x11,  /* Coprocessor 1 (FPU — single-precision only on EE) */
    OP_COP2     = 0x12,  /* Coprocessor 2 (VU0 macro mode) */

    OP_BEQL     = 0x14,  /* Branch on Equal Likely */
    OP_BNEL     = 0x15,  /* Branch on Not Equal Likely */
    OP_BLEZL    = 0x16,  /* Branch on <= 0 Likely */
    OP_BGTZL    = 0x17,  /* Branch on > 0 Likely */

    /* PS2 EE-specific 64-bit immediate ops */
    OP_DADDI    = 0x18,  /* Doubleword Add Immediate */
    OP_DADDIU   = 0x19,  /* Doubleword Add Immediate Unsigned */
    OP_LDL      = 0x1A,  /* Load Doubleword Left */
    OP_LDR      = 0x1B,  /* Load Doubleword Right */

    /* PS2 EE-specific MMI group */
    OP_MMI      = 0x1C,  /* Multimedia Instructions (see r5900_mmi) */

    /* PS2 EE-specific 128-bit load/store */
    OP_LQ       = 0x1E,  /* Load Quadword (128 bits) */
    OP_SQ       = 0x1F,  /* Store Quadword (128 bits) */

    /* Standard MIPS load/store */
    OP_LB       = 0x20,  OP_LH   = 0x21,  OP_LWL  = 0x22,  OP_LW    = 0x23,
    OP_LBU      = 0x24,  OP_LHU  = 0x25,  OP_LWR  = 0x26,  OP_LWU   = 0x27,
    OP_SB       = 0x28,  OP_SH   = 0x29,  OP_SWL  = 0x2A,  OP_SW    = 0x2B,
    OP_SDL      = 0x2C,  OP_SDR  = 0x2D,  OP_SWR  = 0x2E,
    OP_CACHE    = 0x2F,

    OP_LL       = 0x30,  /* Load Linked (atomic) */
    OP_LWC1     = 0x31,  /* Load Word to FPU */
    OP_LWC2     = 0x32,  /* Load Word to COP2 */
    OP_PREF     = 0x33,  /* Prefetch */
    OP_LLD      = 0x34,  /* Load Linked Doubleword */
    OP_LDC1     = 0x35,  /* Load Doubleword to FPU (unused on EE — single prec) */
    OP_LDC2     = 0x36,  /* Load Quadword to VU0 (LQC2) */
    OP_LD       = 0x37,  /* Load Doubleword */

    OP_SC       = 0x38,  /* Store Conditional */
    OP_SWC1     = 0x39,  /* Store Word from FPU */
    OP_SWC2     = 0x3A,  /* Store Word from COP2 */
    OP_SCD      = 0x3C,  /* Store Conditional Doubleword */
    OP_SDC1     = 0x3D,  /* Store Doubleword from FPU (unused on EE) */
    OP_SDC2     = 0x3E,  /* Store Quadword from VU0 (SQC2) */
    OP_SD       = 0x3F,  /* Store Doubleword */
};

/* =========================================================================
 * SPECIAL function field — bits [5:0] when opcode == OP_SPECIAL
 * ========================================================================= */

enum r5900_special {
    SPEC_SLL    = 0x00,
    SPEC_SRL    = 0x02,  SPEC_SRA    = 0x03,
    SPEC_SLLV   = 0x04,
    SPEC_SRLV   = 0x06,  SPEC_SRAV   = 0x07,

    SPEC_JR     = 0x08,  SPEC_JALR   = 0x09,
    SPEC_MOVZ   = 0x0A,  SPEC_MOVN   = 0x0B,
    SPEC_SYSCALL= 0x0C,  SPEC_BREAK  = 0x0D,
    SPEC_SYNC   = 0x0F,

    SPEC_MFHI   = 0x10,  SPEC_MTHI   = 0x11,
    SPEC_MFLO   = 0x12,  SPEC_MTLO   = 0x13,
    SPEC_DSLLV  = 0x14,
    SPEC_DSRLV  = 0x16,  SPEC_DSRAV  = 0x17,

    SPEC_MULT   = 0x18,  SPEC_MULTU  = 0x19,
    SPEC_DIV    = 0x1A,  SPEC_DIVU   = 0x1B,

    SPEC_ADD    = 0x20,  SPEC_ADDU   = 0x21,
    SPEC_SUB    = 0x22,  SPEC_SUBU   = 0x23,
    SPEC_AND    = 0x24,  SPEC_OR     = 0x25,
    SPEC_XOR    = 0x26,  SPEC_NOR    = 0x27,

    /* PS2 EE-specific */
    SPEC_MFSA   = 0x28,  SPEC_MTSA   = 0x29,

    SPEC_SLT    = 0x2A,  SPEC_SLTU   = 0x2B,
    SPEC_DADD   = 0x2C,  SPEC_DADDU  = 0x2D,
    SPEC_DSUB   = 0x2E,  SPEC_DSUBU  = 0x2F,

    /* Traps */
    SPEC_TGE    = 0x30,  SPEC_TGEU   = 0x31,
    SPEC_TLT    = 0x32,  SPEC_TLTU   = 0x33,
    SPEC_TEQ    = 0x34,
    SPEC_TNE    = 0x36,

    /* 64-bit shifts */
    SPEC_DSLL   = 0x38,
    SPEC_DSRL   = 0x3A,  SPEC_DSRA   = 0x3B,
    SPEC_DSLL32 = 0x3C,
    SPEC_DSRL32 = 0x3E,  SPEC_DSRA32 = 0x3F,
};

/* =========================================================================
 * REGIMM sub-opcodes — rt field bits [20:16] when opcode == OP_REGIMM
 * ========================================================================= */

enum r5900_regimm {
    REGIMM_BLTZ     = 0x00,  REGIMM_BGEZ     = 0x01,
    REGIMM_BLTZL    = 0x02,  REGIMM_BGEZL    = 0x03,

    REGIMM_TGEI     = 0x08,  REGIMM_TGEIU    = 0x09,
    REGIMM_TLTI     = 0x0A,  REGIMM_TLTIU    = 0x0B,
    REGIMM_TEQI     = 0x0C,
    REGIMM_TNEI     = 0x0E,

    REGIMM_BLTZAL   = 0x10,  REGIMM_BGEZAL   = 0x11,
    REGIMM_BLTZALL  = 0x12,  REGIMM_BGEZALL  = 0x13,

    /* PS2 EE-specific SA manipulation */
    REGIMM_MTSAB    = 0x18,  REGIMM_MTSAH    = 0x19,
};

/* =========================================================================
 * MMI function field — bits [5:0] when opcode == OP_MMI (0x1C)
 *
 * The Multimedia Instructions are PS2-specific 128-bit SIMD on GPRs.
 * They operate on packed bytes, halfwords, words, or doublewords.
 * ========================================================================= */

enum r5900_mmi {
    MMI_MADD    = 0x00,  MMI_MADDU   = 0x01,
    MMI_MSUB    = 0x02,  MMI_MSUBU   = 0x03,
    MMI_PLZCW   = 0x04,

    MMI_MMI0    = 0x08,  /* Sub-group MMI0 (see r5900_mmi0) */
    MMI_MMI2    = 0x09,  /* Sub-group MMI2 (see r5900_mmi2) */

    MMI_MFHI1   = 0x10,  MMI_MTHI1   = 0x11,
    MMI_MFLO1   = 0x12,  MMI_MTLO1   = 0x13,

    MMI_MULT1   = 0x18,  MMI_MULTU1  = 0x19,
    MMI_DIV1    = 0x1A,  MMI_DIVU1   = 0x1B,

    MMI_MADD1   = 0x20,  MMI_MADDU1  = 0x21,

    MMI_MMI1    = 0x28,  /* Sub-group MMI1 (see r5900_mmi1) */
    MMI_MMI3    = 0x29,  /* Sub-group MMI3 (see r5900_mmi3) */

    MMI_PMFHL   = 0x30,  MMI_PMTHL   = 0x31,

    MMI_PSLLH   = 0x34,
    MMI_PSRLH   = 0x36,  MMI_PSRAH   = 0x37,

    MMI_PSLLW   = 0x3C,
    MMI_PSRLW   = 0x3E,  MMI_PSRAW   = 0x3F,
};

/* ---- MMI0 sub-functions (sa field) ---- */
enum r5900_mmi0 {
    MMI0_PADDW  = 0x00,  MMI0_PSUBW  = 0x01,
    MMI0_PCGTW  = 0x02,  MMI0_PMAXW  = 0x03,
    MMI0_PADDH  = 0x04,  MMI0_PSUBH  = 0x05,
    MMI0_PCGTH  = 0x06,  MMI0_PMAXH  = 0x07,
    MMI0_PADDB  = 0x08,  MMI0_PSUBB  = 0x09,
    MMI0_PCGTB  = 0x0A,

    MMI0_PADDSW = 0x10,  MMI0_PSUBSW = 0x11,
    MMI0_PEXTLW = 0x12,  MMI0_PPACW  = 0x13,
    MMI0_PADDSH = 0x14,  MMI0_PSUBSH = 0x15,
    MMI0_PEXTLH = 0x16,  MMI0_PPACH  = 0x17,
    MMI0_PADDSB = 0x18,  MMI0_PSUBSB = 0x19,
    MMI0_PEXTLB = 0x1A,  MMI0_PPACB  = 0x1B,

    MMI0_PEXT5  = 0x1E,  MMI0_PPAC5  = 0x1F,
};

/* ---- MMI1 sub-functions (sa field) ---- */
enum r5900_mmi1 {
    MMI1_PABSW  = 0x01,  MMI1_PCEQW  = 0x02,  MMI1_PMINW  = 0x03,
    MMI1_PADSBH = 0x04,
    MMI1_PABSH  = 0x05,  MMI1_PCEQH  = 0x06,  MMI1_PMINH  = 0x07,
    MMI1_PCEQB  = 0x0A,

    MMI1_PADDUW = 0x10,  MMI1_PSUBUW = 0x11,  MMI1_PEXTUW = 0x12,
    MMI1_PADDUH = 0x14,  MMI1_PSUBUH = 0x15,  MMI1_PEXTUH = 0x16,
    MMI1_PADDUB = 0x18,  MMI1_PSUBUB = 0x19,  MMI1_PEXTUB = 0x1A,
    MMI1_QFSRV  = 0x1B,
};

/* ---- MMI2 sub-functions (sa field) ---- */
enum r5900_mmi2 {
    MMI2_PMADDW = 0x00,
    MMI2_PSLLVW = 0x02,  MMI2_PSRLVW = 0x03,
    MMI2_PMSUBW = 0x04,
    MMI2_PMFHI  = 0x08,  MMI2_PMFLO  = 0x09,
    MMI2_PINTH  = 0x0A,
    MMI2_PMULTW = 0x0C,  MMI2_PDIVW  = 0x0D,  MMI2_PCPYLD = 0x0E,
    MMI2_PAND   = 0x12,  MMI2_PXOR   = 0x13,
    MMI2_PMADDH = 0x14,  MMI2_PHMADH = 0x15,
    MMI2_PMSUBH = 0x18,  MMI2_PHMSBH = 0x19,
    MMI2_PEXEH  = 0x1A,  MMI2_PREVH  = 0x1B,
    MMI2_PMULTH = 0x1C,  MMI2_PDIVBW = 0x1D,
    MMI2_PEXEW  = 0x1E,  MMI2_PROT3W = 0x1F,
};

/* ---- MMI3 sub-functions (sa field) ---- */
enum r5900_mmi3 {
    MMI3_PMADDUW = 0x00,
    MMI3_PSRAVW  = 0x03,
    MMI3_PMTHI   = 0x08,  MMI3_PMTLO   = 0x09,
    MMI3_PINTEH  = 0x0A,
    MMI3_PMULTUW = 0x0C,  MMI3_PDIVUW  = 0x0D,  MMI3_PCPYUD = 0x0E,
    MMI3_POR     = 0x12,  MMI3_PNOR    = 0x13,
    MMI3_PEXCH   = 0x1A,  MMI3_PCPYH   = 0x1B,
    MMI3_PEXCW   = 0x1E,
};

/* ---- PMFHL/PMTHL variations (sa field) ---- */
enum r5900_pmfhl {
    PMFHL_LW  = 0x00,
    PMFHL_UW  = 0x01,
    PMFHL_SLW = 0x02,
    PMFHL_LH  = 0x03,
    PMFHL_SH  = 0x04,
};

/* =========================================================================
 * COP0 (System Control Coprocessor)
 *
 * Handles TLB, exceptions, timers, interrupt enable/disable.
 * See: ps2tek "EE Overview" and "EE Timers" sections
 * ========================================================================= */

enum r5900_cop0_format {
    COP0_MF  = 0x00,   /* MFC0 — move from COP0 register */
    COP0_MT  = 0x04,   /* MTC0 — move to COP0 register */
    COP0_BC  = 0x08,   /* BC0x — branch on COP0 cond */
    COP0_CO  = 0x10,   /* CO — COP0 operation */
};

enum r5900_cop0_co {
    COP0_TLBR  = 0x01,  COP0_TLBWI = 0x02,
    COP0_TLBWR = 0x06,  COP0_TLBP  = 0x08,
    COP0_ERET  = 0x18,  /* Return from exception */
    COP0_EI    = 0x38,  /* Enable Interrupts */
    COP0_DI    = 0x39,  /* Disable Interrupts */
};

enum r5900_cop0_bc {
    COP0_BCF  = 0x00,  COP0_BCT  = 0x01,
    COP0_BCFL = 0x02,  COP0_BCTL = 0x03,
};

enum r5900_cop0_reg {
    COP0R_INDEX    = 0,   COP0R_RANDOM   = 1,
    COP0R_ENTRYLO0 = 2,   COP0R_ENTRYLO1 = 3,
    COP0R_CONTEXT  = 4,   COP0R_PAGEMASK = 5,
    COP0R_WIRED    = 6,
    COP0R_BADVADDR = 8,   COP0R_COUNT    = 9,
    COP0R_ENTRYHI  = 10,  COP0R_COMPARE  = 11,
    COP0R_STATUS   = 12,  COP0R_CAUSE    = 13,
    COP0R_EPC      = 14,  COP0R_PRID     = 15,
    COP0R_CONFIG   = 16,
    COP0R_BADPADDR = 23,  COP0R_DEBUG    = 24,
    COP0R_PERF     = 25,
    COP0R_TAGLO    = 28,  COP0R_TAGHI    = 29,
    COP0R_ERROREPC = 30,
};

/* =========================================================================
 * COP1 (FPU) — single-precision only on EE
 *
 * The EE FPU is non-IEEE-754 compliant in several ways:
 * - No denormals (flushed to zero)
 * - No NaN propagation differences
 * - Accumulator register (ACC / f[31])
 * - Extra ops: ADDA, SUBA, MULA, MADDA, MSUBA, RSQRT, MAX, MIN
 * ========================================================================= */

enum r5900_cop1_format {
    COP1_MF = 0x00,  COP1_CF = 0x02,
    COP1_MT = 0x04,  COP1_CT = 0x06,
    COP1_BC = 0x08,
    COP1_S  = 0x10,  /* Single-precision operations */
    COP1_W  = 0x14,  /* Word (integer) operations */
};

enum r5900_cop1_s {
    COP1S_ADD     = 0x00,  COP1S_SUB     = 0x01,
    COP1S_MUL     = 0x02,  COP1S_DIV     = 0x03,
    COP1S_SQRT    = 0x04,  COP1S_ABS     = 0x05,
    COP1S_MOV     = 0x06,  COP1S_NEG     = 0x07,

    COP1S_ROUND_W = 0x0C,  COP1S_TRUNC_W = 0x0D,
    COP1S_CEIL_W  = 0x0E,  COP1S_FLOOR_W = 0x0F,

    /* PS2 EE-specific FPU extensions */
    COP1S_RSQRT   = 0x16,
    COP1S_ADDA    = 0x18,  COP1S_SUBA    = 0x19,
    COP1S_MULA    = 0x1A,
    COP1S_MADD    = 0x1C,  COP1S_MSUB    = 0x1D,
    COP1S_MADDA   = 0x1E,  COP1S_MSUBA   = 0x1F,

    COP1S_CVT_W   = 0x24,

    COP1S_MAX     = 0x28,  COP1S_MIN     = 0x29,

    /* FPU compare instructions (set condition bit in FCR31) */
    COP1S_C_F     = 0x30,  COP1S_C_UN    = 0x31,
    COP1S_C_EQ    = 0x32,  COP1S_C_UEQ   = 0x33,
    COP1S_C_OLT   = 0x34,  COP1S_C_ULT   = 0x35,
    COP1S_C_OLE   = 0x36,  COP1S_C_ULE   = 0x37,
    COP1S_C_SF    = 0x38,  COP1S_C_NGLE  = 0x39,
    COP1S_C_SEQ   = 0x3A,  COP1S_C_NGL   = 0x3B,
    COP1S_C_LT    = 0x3C,  COP1S_C_NGE   = 0x3D,
    COP1S_C_LE    = 0x3E,  COP1S_C_NGT   = 0x3F,
};

enum r5900_cop1_w {
    COP1W_CVT_S = 0x20,
};

enum r5900_cop1_bc {
    COP1_BCF  = 0x00,  COP1_BCT  = 0x01,
    COP1_BCFL = 0x02,  COP1_BCTL = 0x03,
};

/* =========================================================================
 * COP2 (VU0 in macro mode) — vector float SIMD
 *
 * VU0 macro mode exposes VU0's vector registers (vf[0..31]) and integer
 * registers (vi[0..15]) to the EE via COP2 instructions.
 * The EE can issue VU0 macro ops inline with normal EE code.
 *
 * VU0 has 32 vector float registers (128-bit xyzw), shared Q/P/I/ACC regs.
 * ========================================================================= */

enum r5900_cop2_format {
    COP2_QMFC2 = 0x01,  COP2_CFC2  = 0x02,
    COP2_QMTC2 = 0x05,  COP2_CTC2  = 0x06,
    COP2_BC    = 0x08,
    COP2_CO    = 0x10,   /* VU0 macro operation (format >= 0x10) */
};

enum r5900_cop2_bc {
    COP2_BCF  = 0x00,  COP2_BCT  = 0x01,
    COP2_BCFL = 0x02,  COP2_BCTL = 0x03,
};

/* VU0 Special1 functions — lower 6 bits when COP2 CO, func < 0x3C */
enum r5900_vu0_s1 {
    VU0S1_VADDx   = 0x00, VU0S1_VADDy   = 0x01,
    VU0S1_VADDz   = 0x02, VU0S1_VADDw   = 0x03,
    VU0S1_VSUBx   = 0x04, VU0S1_VSUBy   = 0x05,
    VU0S1_VSUBz   = 0x06, VU0S1_VSUBw   = 0x07,
    VU0S1_VMADDx  = 0x08, VU0S1_VMADDy  = 0x09,
    VU0S1_VMADDz  = 0x0A, VU0S1_VMADDw  = 0x0B,
    VU0S1_VMSUBx  = 0x0C, VU0S1_VMSUBy  = 0x0D,
    VU0S1_VMSUBz  = 0x0E, VU0S1_VMSUBw  = 0x0F,
    VU0S1_VMAXx   = 0x10, VU0S1_VMAXy   = 0x11,
    VU0S1_VMAXz   = 0x12, VU0S1_VMAXw   = 0x13,
    VU0S1_VMINIx  = 0x14, VU0S1_VMINIy  = 0x15,
    VU0S1_VMINIz  = 0x16, VU0S1_VMINIw  = 0x17,
    VU0S1_VMULx   = 0x18, VU0S1_VMULy   = 0x19,
    VU0S1_VMULz   = 0x1A, VU0S1_VMULw   = 0x1B,
    VU0S1_VMULq   = 0x1C, VU0S1_VMAXi   = 0x1D,
    VU0S1_VMULi   = 0x1E, VU0S1_VMINIi  = 0x1F,
    VU0S1_VADDq   = 0x20, VU0S1_VMADDq  = 0x21,
    VU0S1_VADDi   = 0x22, VU0S1_VMADDi  = 0x23,
    VU0S1_VSUBq   = 0x24, VU0S1_VMSUBq  = 0x25,
    VU0S1_VSUBi   = 0x26, VU0S1_VMSUBi  = 0x27,
    VU0S1_VADD    = 0x28, VU0S1_VMADD   = 0x29,
    VU0S1_VMUL    = 0x2A, VU0S1_VMAX    = 0x2B,
    VU0S1_VSUB    = 0x2C, VU0S1_VMSUB   = 0x2D,
    VU0S1_VOPMSUB = 0x2E, VU0S1_VMINI   = 0x2F,
    VU0S1_VIADD   = 0x30, VU0S1_VISUB   = 0x31,
    VU0S1_VIADDI  = 0x32,
    VU0S1_VIAND   = 0x34, VU0S1_VIOR    = 0x35,
    VU0S1_VCALLMS = 0x38, VU0S1_VCALLMSR= 0x39,
};

/* VU0 Special2 functions — lower 6 bits when COP2 CO, func >= 0x3C */
enum r5900_vu0_s2 {
    VU0S2_VADDAx  = 0x00, VU0S2_VADDAy  = 0x01,
    VU0S2_VADDAz  = 0x02, VU0S2_VADDAw  = 0x03,
    VU0S2_VSUBAx  = 0x04, VU0S2_VSUBAy  = 0x05,
    VU0S2_VSUBAz  = 0x06, VU0S2_VSUBAw  = 0x07,
    VU0S2_VMADDAx = 0x08, VU0S2_VMADDAy = 0x09,
    VU0S2_VMADDAz = 0x0A, VU0S2_VMADDAw = 0x0B,
    VU0S2_VMSUBAx = 0x0C, VU0S2_VMSUBAy = 0x0D,
    VU0S2_VMSUBAz = 0x0E, VU0S2_VMSUBAw = 0x0F,
    VU0S2_VITOF0  = 0x10, VU0S2_VITOF4  = 0x11,
    VU0S2_VITOF12 = 0x12, VU0S2_VITOF15 = 0x13,
    VU0S2_VFTOI0  = 0x14, VU0S2_VFTOI4  = 0x15,
    VU0S2_VFTOI12 = 0x16, VU0S2_VFTOI15 = 0x17,
    VU0S2_VMULAx  = 0x18, VU0S2_VMULAy  = 0x19,
    VU0S2_VMULAz  = 0x1A, VU0S2_VMULAw  = 0x1B,
    VU0S2_VMULAq  = 0x1C, VU0S2_VABS    = 0x1D,
    VU0S2_VMULAi  = 0x1E, VU0S2_VCLIPw  = 0x1F,
    VU0S2_VADDAq  = 0x20, VU0S2_VMADDAq = 0x21,
    VU0S2_VADDAi  = 0x22, VU0S2_VMADDAi = 0x23,
    VU0S2_VSUBAq  = 0x24, VU0S2_VMSUBAq = 0x25,
    VU0S2_VSUBAi  = 0x26, VU0S2_VMSUBAi = 0x27,
    VU0S2_VADDA   = 0x28, VU0S2_VMADDA  = 0x29,
    VU0S2_VMULA   = 0x2A,
    VU0S2_VSUBA   = 0x2C, VU0S2_VMSUBA  = 0x2D,
    VU0S2_VOPMULA = 0x2E, VU0S2_VNOP    = 0x2F,
    VU0S2_VMOVE   = 0x30, VU0S2_VMR32   = 0x31,
    VU0S2_VRNEXT  = 0x32, VU0S2_VRGET   = 0x33,
    VU0S2_VRINIT  = 0x34, VU0S2_VRXOR   = 0x35,
    VU0S2_VLQI    = 0x36, VU0S2_VSQI    = 0x37,
    VU0S2_VLQD    = 0x38, VU0S2_VSQD    = 0x39,
    VU0S2_VDIV    = 0x3A, VU0S2_VSQRT   = 0x3B,
    VU0S2_VRSQRT  = 0x3C, VU0S2_VWAITQ  = 0x3D,
    VU0S2_VMTIR   = 0x3E, VU0S2_VMFIR   = 0x3F,
    VU0S2_VILWR   = 0x40, VU0S2_VISWR   = 0x41,
};

/* VU0 control registers (used with CFC2/CTC2) */
enum r5900_vu0_creg {
    VU0CR_STATUS    = 0,   VU0CR_MAC       = 1,
    VU0CR_VPU_STAT  = 2,   VU0CR_R         = 3,
    VU0CR_I         = 4,   VU0CR_CLIP      = 5,
    VU0CR_TPC       = 6,   VU0CR_CMSAR0    = 7,
    VU0CR_FBRST     = 8,
    VU0CR_ACC       = 20,  VU0CR_P         = 26,
    VU0CR_XITOP     = 27,  VU0CR_ITOP      = 28,
    VU0CR_TOP       = 29,
};

/* =========================================================================
 * Decoded instruction structure
 * ========================================================================= */

/** Flags for instruction classification */
typedef struct {
    bool is_mmi;         /**< Is a Multimedia Instruction (OP_MMI) */
    bool is_vu;          /**< Is a VU0 macro instruction (COP2) */
    bool is_branch;      /**< Is a conditional branch */
    bool is_jump;        /**< Is an unconditional jump (J/JR/JAL/JALR) */
    bool is_call;        /**< Is a function call (JAL/JALR/BGEZALx/BLTZALx) */
    bool is_return;      /**< Is a subroutine return (JR $ra / ERET) */
    bool has_delay_slot; /**< Has a delay slot (all branches/jumps on MIPS) */
    bool is_multimedia;  /**< PS2-specific multimedia (MMI or VU) */
    bool is_load;        /**< Reads from memory */
    bool is_store;       /**< Writes to memory */

    bool modifies_gpr;     /**< Writes a general-purpose register */
    bool modifies_fpr;     /**< Writes a floating-point register */
    bool modifies_vfr;     /**< Writes a VU vector float register */
    bool modifies_vir;     /**< Writes a VU integer register */
    bool modifies_control; /**< Modifies control state (PC/HI/LO/COP0/flags) */
    bool modifies_memory;  /**< Writes to memory */
} r5900_inst_flags_t;

/** Full decoded instruction */
typedef struct {
    uint32_t address;      /**< PC address of this instruction */
    uint32_t raw;          /**< Raw 32-bit MIPS instruction word */

    /* Decoded fields (always extracted) */
    uint32_t opcode;       /**< Primary opcode [31:26] */
    uint32_t rs;           /**< Source register [25:21] */
    uint32_t rt;           /**< Target register [20:16] */
    uint32_t rd;           /**< Destination register [15:11] */
    uint32_t sa;           /**< Shift amount [10:6] */
    uint32_t function;     /**< Function field [5:0] */
    uint32_t immediate;    /**< Unsigned 16-bit immediate */
    int32_t  simmediate;   /**< Sign-extended 16-bit immediate */
    uint32_t target;       /**< 26-bit jump target */

    /* MMI sub-type info */
    uint8_t  mmi_type;     /**< 0=MMI0 1=MMI1 2=MMI2 3=MMI3 */
    uint8_t  mmi_function; /**< Sub-function within MMI type */
    uint8_t  pmfhl_var;    /**< PMFHL variation (sa field) */

    /* VU vector info */
    uint8_t  vu_dest;      /**< xyzw destination mask (4 bits) */
    uint8_t  vu_fsf;       /**< FS field select (bits 10-11) */
    uint8_t  vu_ftf;       /**< FT field select (bits 8-9) */

    /* Classification flags */
    r5900_inst_flags_t flags;
} r5900_inst_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Decode a single 32-bit MIPS instruction into r5900_inst_t.
 *
 * Extracts all fields, classifies the instruction type, and sets flags.
 * This is the pure-C equivalent of R5900Decoder::decodeInstruction().
 */
void r5900_decode(uint32_t address, uint32_t raw, r5900_inst_t *out);

/**
 * @brief Return the branch target address for a branch instruction.
 * @return Target address, or 0 if not a branch.
 */
uint32_t r5900_branch_target(const r5900_inst_t *inst);

/**
 * @brief Return the jump target address for a J/JAL instruction.
 * @return Target address, or 0 if not a static jump.
 */
uint32_t r5900_jump_target(const r5900_inst_t *inst);

/**
 * @brief Get a human-readable mnemonic string for the decoded instruction.
 *
 * Writes the mnemonic (e.g. "ADDIU", "LW", "VADD.xyzw") into buf.
 * @return Number of characters written (excluding NUL), or 0 on failure.
 */
int r5900_disasm(const r5900_inst_t *inst, char *buf, size_t buf_size);

/**
 * @brief Decode a buffer of MIPS instructions.
 *
 * @param code        Pointer to raw MIPS binary (little-endian)
 * @param code_size   Size of the buffer in bytes (must be multiple of 4)
 * @param base_addr   Virtual address of the first instruction
 * @param out         Output array (caller must allocate code_size/4 entries)
 * @param out_count   Set to the number of decoded instructions
 */
void r5900_decode_buffer(const uint8_t *code, size_t code_size,
                         uint32_t base_addr,
                         r5900_inst_t *out, size_t *out_count);

/**
 * @brief Disassemble code to a file (replaces old disassemble_code).
 *
 * Uses the built-in R5900 decoder (no Capstone dependency for MIPS).
 */
void r5900_disasm_to_file(const uint8_t *code, size_t code_size,
                          uint32_t base_addr, const char *output_path);

#ifdef __cplusplus
}
#endif

#endif /* R5900_DECOMPILER_H */
