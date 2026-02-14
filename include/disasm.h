#ifndef DISASM_H
#define DISASM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Disassembles binary code and writes the result to an .asm file.
 *
 * Uses the Capstone disassembly framework to decode MIPS instructions
 * from a raw binary buffer and writes them to out/<name>.asm.
 *
 * @param code      Pointer to binary code buffer
 * @param code_size Size of the binary code in bytes
 * @param name      Base name for the output .asm file
 */
void disassemble_code(const uint8_t *code, size_t code_size, const char *name);

#endif /* DISASM_H */
