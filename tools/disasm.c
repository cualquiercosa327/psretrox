#include "../include/disasm.h"
#include "../include/file_utils.h"
#include <stdio.h>
#include <string.h>
#include <capstone/capstone.h>

void disassemble_code(const uint8_t *code, size_t code_size, const char *name) {
    if (!code || code_size == 0 || !name) {
        fprintf(stderr, "disassemble_code: invalid parameters\n");
        return;
    }

    /* Build output path */
    char log_path[1024];
    snprintf(log_path, sizeof(log_path), "out/%s.asm", name);

    /* Ensure output directory exists */
    create_directories("out");

    FILE *log_file = fopen(log_path, "w");
    if (!log_file) {
        fprintf(stderr, "Error opening output file: %s\n", log_path);
        return;
    }

    /* Initialize Capstone for MIPS (PS2 uses MIPS R5900, little-endian) */
    csh handle;
    cs_err err = cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN, &handle);
    if (err != CS_ERR_OK) {
        fprintf(stderr, "Capstone init error: %s\n", cs_strerror(err));
        fclose(log_file);
        return;
    }

    cs_insn *insn;
    size_t count = cs_disasm(handle, code, code_size, 0x1000, 0, &insn);

    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            fprintf(log_file, "0x%08" PRIx64 ":\t%s\t%s\n",
                    insn[i].address, insn[i].mnemonic, insn[i].op_str);
        }
        cs_free(insn, count);
    } else {
        fprintf(stderr, "Failed to disassemble code for: %s\n", name);
    }

    cs_close(&handle);
    fclose(log_file);

    printf("Disassembly saved to: %s (%zu instructions)\n", log_path, count);
}
