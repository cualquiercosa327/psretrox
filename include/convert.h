#ifndef CONVERT_H
#define CONVERT_H

#include <stdio.h>

/**
 * @brief Translates a single assembly instruction to pseudo-C.
 *
 * Writes the C equivalent of the instruction to the output file.
 * Covers x86 arithmetic, logic, jumps, FPU, string and flag operations.
 *
 * @param instruction  The assembly instruction line (e.g. "add eax, ebx")
 * @param out          Output FILE pointer to write the C translation
 */
void process_instruction(const char *instruction, FILE *out);

/**
 * @brief Converts an assembly log file to pseudo-C source.
 *
 * Reads each line from the input .asm file, translates it via
 * process_instruction(), and writes the result to the output file.
 *
 * @param log_path     Path to the input assembly log (.asm)
 * @param output_path  Path to the output C file (.c)
 */
void convert_to_c(const char *log_path, const char *output_path);

#endif /* CONVERT_H */
