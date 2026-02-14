/**
 * @file recompiler.h
 * @brief R5900 instruction-to-C translator (code generator) — pure C port.
 *
 * Migrated from ps2recomp's code_generator.h / code_generator.cpp (C++).
 * Replaces the old tools/convert.c which only handled x86 assembly.
 * Now translates decoded R5900 MIPS instructions into C source code.
 *
 * References:
 *   - ps2recomp  ps2xRecomp/src/code_generator.cpp   (original C++ source)
 *   - ps2recomp  ps2xRecomp/include/ps2recomp/code_generator.h
 *   - "See MIPS Run" (Dominic Sweetman)
 *   - ps2tek  https://psi-rockin.github.io/ps2tek/
 *   - Copetti PS2 Architecture
 *
 * Design notes:
 *   - All fmt::format calls replaced with snprintf
 *   - SSE intrinsics replaced with portable C macros/helpers
 *   - C++ std::string replaced with char buffers (caller-allocated)
 *   - No classes, no namespaces — plain C with snake_case
 *   - Each translate function writes into a caller-provided buffer
 *   - Return value is the number of bytes written (like snprintf)
 */

#ifndef RECOMPILER_H
#define RECOMPILER_H

#include "r5900_decompiler.h"  /* r5900_inst_t, opcode enums */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

/** Maximum size for a single translated instruction string */
#define RECOMP_LINE_MAX     2048

/** Maximum size for a full function output buffer */
#define RECOMP_FUNC_MAX     (1024 * 1024)  /* 1 MiB */

/* =========================================================================
 * Translated instruction output
 * ========================================================================= */

/**
 * Translate a single decoded R5900 instruction to C code.
 *
 * The output is a single C statement (or comment) written into @p buf.
 * Returns the number of characters written (excluding NUL), or negative
 * on error, following snprintf semantics.
 *
 * @param inst      Pointer to decoded instruction (from r5900_decode).
 * @param buf       Output buffer for the C code string.
 * @param buf_size  Size of @p buf in bytes.
 * @return          Characters written (excl. NUL), or < 0 on error.
 */
int recomp_translate(const r5900_inst_t *inst, char *buf, size_t buf_size);

/* ---- Category-specific translators (called by recomp_translate) ---- */

int recomp_translate_special(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_regimm(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_cop0(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_fpu(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_vu(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_mmi(const r5900_inst_t *inst, char *buf, size_t sz);

/* ---- MMI sub-decoders ---- */
int recomp_translate_mmi0(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_mmi1(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_mmi2(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_mmi3(const r5900_inst_t *inst, char *buf, size_t sz);
int recomp_translate_pmfhl(const r5900_inst_t *inst, char *buf, size_t sz);

/* =========================================================================
 * Batch translation — process a whole buffer of instructions
 * ========================================================================= */

/**
 * Translate a buffer of decoded instructions to C code, writing to a file.
 *
 * @param insts         Array of decoded instructions.
 * @param count         Number of instructions.
 * @param func_name     Name to give the generated C function.
 * @param output_path   Path to write the .c file.
 */
void recomp_translate_to_file(const r5900_inst_t *insts, size_t count,
                              const char *func_name, const char *output_path);

/**
 * Translate raw binary code to a C file (decode + translate in one step).
 *
 * @param code          Raw MIPS binary (little-endian).
 * @param code_size     Size of @p code in bytes.
 * @param base_addr     Base virtual address of the code section.
 * @param func_name     Name to give the generated C function.
 * @param output_path   Path to write the .c file.
 */
void recomp_binary_to_c(const uint8_t *code, size_t code_size,
                         uint32_t base_addr, const char *func_name,
                         const char *output_path);

#ifdef __cplusplus
}
#endif

#endif /* RECOMPILER_H */
