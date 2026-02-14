#include "../include/convert.h"
#include <string.h>
#include <stdio.h>

/* Write standard includes at the top of the generated file */
static void write_preamble(FILE *out) {
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <string.h>\n\n");
}

void process_instruction(const char *instruction, FILE *out) {
    if (!instruction || !out) return;

    char op[64] = {0};
    char arg1[128] = {0};
    char arg2[128] = {0};

    int nargs = sscanf(instruction, "%63s %127[^,], %127s", op, arg1, arg2);
    if (nargs < 1) {
        fprintf(out, "// (empty line)\n");
        return;
    }

    /* Remove trailing comma from arg1 if present */
    size_t len = strlen(arg1);
    if (len > 0 && arg1[len - 1] == ',')
        arg1[len - 1] = '\0';

    /* ---- Arithmetic ---- */
    if (strcmp(op, "add") == 0) {
        fprintf(out, "%s += %s;\n", arg1, arg2);
    } else if (strcmp(op, "sub") == 0) {
        fprintf(out, "%s -= %s;\n", arg1, arg2);
    } else if (strcmp(op, "inc") == 0) {
        fprintf(out, "%s += 1;\n", arg1);
    } else if (strcmp(op, "dec") == 0) {
        fprintf(out, "%s -= 1;\n", arg1);
    } else if (strcmp(op, "imul") == 0) {
        fprintf(out, "%s *= %s; // Integer Multiply\n", arg1, arg2);
    } else if (strcmp(op, "idiv") == 0) {
        fprintf(out, "EDX_EAX /= %s; // Integer Division\n", arg1);
    } else if (strcmp(op, "adc") == 0) {
        fprintf(out, "%s += %s + CF; // Add with Carry\n", arg1, arg2);
    } else if (strcmp(op, "sbb") == 0) {
        fprintf(out, "%s = %s - (%s + CF); // Subtract with Borrow\n", arg1, arg1, arg2);

    /* ---- Logic ---- */
    } else if (strcmp(op, "and") == 0) {
        fprintf(out, "%s &= %s;\n", arg1, arg2);
    } else if (strcmp(op, "or") == 0) {
        fprintf(out, "%s |= %s;\n", arg1, arg2);
    } else if (strcmp(op, "xor") == 0) {
        fprintf(out, "%s ^= %s;\n", arg1, arg2);
    } else if (strcmp(op, "not") == 0) {
        fprintf(out, "%s = ~%s; // Logical NOT\n", arg1, arg1);
    } else if (strcmp(op, "shl") == 0) {
        fprintf(out, "%s <<= %s; // Shift Left\n", arg1, arg2);
    } else if (strcmp(op, "sar") == 0) {
        fprintf(out, "%s >>= %s; // Arithmetic Shift Right\n", arg1, arg2);
    } else if (strcmp(op, "rol") == 0) {
        fprintf(out, "rotate_left(%s, %s);\n", arg1, arg2);
    } else if (strcmp(op, "rcl") == 0) {
        fprintf(out, "rotate_carry_left(%s); // Rotate Carry Left\n", arg1);
    } else if (strcmp(op, "rcr") == 0) {
        fprintf(out, "rotate_carry_right(%s, %s); // Rotate Carry Right\n", arg1, arg2);

    /* ---- Data Movement ---- */
    } else if (strcmp(op, "mov") == 0) {
        fprintf(out, "%s = %s;\n", arg1, arg2);
    } else if (strcmp(op, "lea") == 0) {
        fprintf(out, "%s = &(%s); // Load Effective Address\n", arg1, arg2);
    } else if (strcmp(op, "xchg") == 0) {
        fprintf(out, "swap(%s, %s);\n", arg1, arg2);
    } else if (strcmp(op, "push") == 0) {
        fprintf(out, "stack_push(%s);\n", arg1);
    } else if (strcmp(op, "pop") == 0) {
        fprintf(out, "%s = stack_pop();\n", arg1);
    } else if (strcmp(op, "cwde") == 0) {
        fprintf(out, "eax = (int32_t)(int16_t)eax; // Sign-extend word to dword\n");

    /* ---- Compare & Test ---- */
    } else if (strcmp(op, "cmp") == 0) {
        fprintf(out, "if (%s != %s) { /* action */ };\n", arg1, arg2);
    } else if (strcmp(op, "test") == 0) {
        fprintf(out, "if ((%s & %s) == 0) { /* action */ };\n", arg1, arg2);

    /* ---- Jumps ---- */
    } else if (strcmp(op, "jmp") == 0) {
        fprintf(out, "goto %s;\n", arg1);
    } else if (strcmp(op, "je") == 0) {
        fprintf(out, "if (ZF == 1) goto %s; // Jump if Equal\n", arg1);
    } else if (strcmp(op, "jg") == 0) {
        fprintf(out, "if (SF == OF && ZF == 0) goto %s; // Jump if Greater\n", arg1);
    } else if (strcmp(op, "jge") == 0) {
        fprintf(out, "if (SF == OF) goto %s; // Jump if Greater or Equal\n", arg1);
    } else if (strcmp(op, "jle") == 0) {
        fprintf(out, "if (ZF || SF != OF) goto %s; // Jump if Less or Equal\n", arg1);
    } else if (strcmp(op, "jae") == 0) {
        fprintf(out, "if (CF == 0) goto %s; // Jump if Above or Equal\n", arg1);
    } else if (strcmp(op, "jb") == 0) {
        fprintf(out, "if (CF == 1) goto %s; // Jump if Below\n", arg1);
    } else if (strcmp(op, "jbe") == 0) {
        fprintf(out, "if (CF == 1 || ZF == 1) goto %s; // Jump if Below or Equal\n", arg1);
    } else if (strcmp(op, "ja") == 0) {
        fprintf(out, "if (CF == 0 && ZF == 0) goto %s; // Jump if Above\n", arg1);
    } else if (strcmp(op, "jo") == 0) {
        fprintf(out, "if (OF) goto %s; // Jump if Overflow\n", arg1);
    } else if (strcmp(op, "jno") == 0) {
        fprintf(out, "if (!OF) goto %s; // Jump if No Overflow\n", arg1);
    } else if (strcmp(op, "js") == 0) {
        fprintf(out, "if (SF == 1) goto %s; // Jump if Signed\n", arg1);
    } else if (strcmp(op, "jp") == 0) {
        fprintf(out, "if (PF == 1) goto %s; // Jump if Parity\n", arg1);
    } else if (strcmp(op, "jecxz") == 0) {
        fprintf(out, "if (ecx == 0) goto %s; // Jump if ECX is zero\n", arg1);

    /* ---- Loops ---- */
    } else if (strcmp(op, "loop") == 0) {
        fprintf(out, "if (--ecx != 0) goto %s; // Loop\n", arg1);
    } else if (strcmp(op, "loope") == 0) {
        fprintf(out, "if (--ECX && ZF) goto %s; // Loop while Equal\n", arg1);
    } else if (strcmp(op, "loopne") == 0) {
        fprintf(out, "if (ecx != 0 && ZF == 0) goto %s; // Loop while Not Equal\n", arg1);

    /* ---- Call / Return ---- */
    } else if (strcmp(op, "call") == 0) {
        fprintf(out, "call_function(%s);\n", arg1);
    } else if (strcmp(op, "ret") == 0) {
        fprintf(out, "return;\n");
    } else if (strcmp(op, "leave") == 0) {
        fprintf(out, "ebp = stack_pop(); // Leave function (restore stack)\n");

    /* ---- Stack ---- */
    } else if (strcmp(op, "pushal") == 0) {
        fprintf(out, "// pushal: save all general registers\n");
        fprintf(out, "stack_push(eax); stack_push(ebx); stack_push(ecx); stack_push(edx);\n");
        fprintf(out, "stack_push(esi); stack_push(edi); stack_push(ebp);\n");
    } else if (strcmp(op, "popal") == 0) {
        fprintf(out, "// popal: restore all general registers\n");
        fprintf(out, "edi = stack_pop(); esi = stack_pop(); ebp = stack_pop();\n");
        fprintf(out, "ebx = stack_pop(); edx = stack_pop(); ecx = stack_pop(); eax = stack_pop();\n");
    } else if (strcmp(op, "pushfd") == 0) {
        fprintf(out, "stack_push(EFLAGS); // Push Flags\n");

    /* ---- Flags ---- */
    } else if (strcmp(op, "cld") == 0) {
        fprintf(out, "DF = 0; // Clear Direction Flag\n");
    } else if (strcmp(op, "clc") == 0) {
        fprintf(out, "CF = 0; // Clear Carry Flag\n");
    } else if (strcmp(op, "cmc") == 0) {
        fprintf(out, "CF = !CF; // Complement Carry Flag\n");
    } else if (strcmp(op, "cli") == 0) {
        fprintf(out, "IF = 0; // Clear Interrupt Flag\n");
    } else if (strcmp(op, "lahf") == 0) {
        fprintf(out, "ah = (SF << 7) | (ZF << 6) | (AF << 4) | (PF << 2) | CF; // Load Flags into AH\n");

    /* ---- String Ops ---- */
    } else if (strcmp(op, "movsb") == 0) {
        fprintf(out, "*edi++ = *esi++; // Move Byte String\n");
    } else if (strcmp(op, "movsd") == 0) {
        fprintf(out, "*(uint32_t*)edi = *(uint32_t*)esi; edi += 4; esi += 4; // Move DWORD String\n");
    } else if (strcmp(op, "lodsb") == 0) {
        fprintf(out, "al = *esi++; // Load String Byte\n");
    } else if (strcmp(op, "lodsd") == 0) {
        fprintf(out, "eax = *(uint32_t*)esi; esi += 4; // Load String DWORD\n");
    } else if (strcmp(op, "scasb") == 0) {
        fprintf(out, "ZF = (al == *edi); edi++; // Compare String Byte\n");
    } else if (strcmp(op, "scasd") == 0) {
        fprintf(out, "ZF = (eax == *(uint32_t*)edi); edi += 4; // Compare String DWORD\n");

    /* ---- I/O Ports ---- */
    } else if (strcmp(op, "in") == 0) {
        fprintf(out, "%s = port_in(%s);\n", arg1, arg2);
    } else if (strcmp(op, "out") == 0) {
        fprintf(out, "port_out(%s, %s);\n", arg1, arg2);
    } else if (strcmp(op, "insb") == 0) {
        fprintf(out, "*edi++ = port_in(dx); // Input Byte\n");
    } else if (strcmp(op, "insd") == 0) {
        fprintf(out, "*(uint32_t*)edi = port_in(dx); edi += 4; // Input DWORD\n");
    } else if (strcmp(op, "outsb") == 0) {
        fprintf(out, "port_out(dx, *esi++); // Output Byte\n");
    } else if (strcmp(op, "outsd") == 0) {
        fprintf(out, "port_out(dx, *(uint32_t*)esi); esi += 4; // Output DWORD\n");

    /* ---- FPU ---- */
    } else if (strcmp(op, "fld") == 0) {
        fprintf(out, "st0 = *(float*)%s; // FPU Load\n", arg1);
    } else if (strcmp(op, "fadd") == 0) {
        fprintf(out, "st0 += *(float*)%s; // FPU Add\n", arg1);
    } else if (strcmp(op, "fsubr") == 0) {
        fprintf(out, "st0 = %s - st0; // FPU Reverse Subtract\n", arg1);
    } else if (strcmp(op, "fistp") == 0) {
        fprintf(out, "*(%s) = (int)st0; // FPU Store Integer and Pop\n", arg1);

    /* ---- SIMD ---- */
    } else if (strcmp(op, "pavgb") == 0) {
        fprintf(out, "%s = (%s + %s) / 2; // Packed Average Bytes\n", arg1, arg1, arg2);
    } else if (strcmp(op, "psubd") == 0) {
        fprintf(out, "%s -= %s; // Subtract Packed DWORD\n", arg1, arg2);

    /* ---- Interrupts / Debug ---- */
    } else if (strcmp(op, "int3") == 0) {
        fprintf(out, "debug_breakpoint(); // INT 3\n");
    } else if (strcmp(op, "int1") == 0) {
        fprintf(out, "debug_trap(); // INT 1\n");
    } else if (strcmp(op, "int") == 0) {
        fprintf(out, "interrupt(%s); // System interrupt\n", arg1);

    /* ---- Misc ---- */
    } else if (strcmp(op, "nop") == 0) {
        fprintf(out, "// nop\n");
    } else if (strcmp(op, "invd") == 0) {
        fprintf(out, "// invd: Invalidate caches (no C equivalent)\n");
    } else if (strcmp(op, "arpl") == 0) {
        fprintf(out, "// arpl: Segment privilege adjust (no C equivalent)\n");
    } else if (strcmp(op, "lds") == 0) {
        fprintf(out, "%s = %s; // Load pointer and DS segment\n", arg1, arg2);
    } else if (strcmp(op, "les") == 0) {
        fprintf(out, "%s = load_pointer(%s); // Load ES Segment\n", arg1, arg2);
    } else if (strcmp(op, "sldt") == 0) {
        fprintf(out, "ldt = %s; // Store Local Descriptor Table\n", arg1);
    } else if (strcmp(op, "daa") == 0) {
        fprintf(out, "// daa: Decimal Adjust AL (no C equivalent)\n");
    } else if (strcmp(op, "das") == 0) {
        fprintf(out, "// das: Decimal Adjust after Subtraction (no C equivalent)\n");
    } else if (strcmp(op, "aaa") == 0) {
        fprintf(out, "if ((AL & 0xF) > 9 || AF) { AL += 6; AH += 1; } AL &= 0xF; // AAA\n");
    } else if (strcmp(op, "aad") == 0) {
        fprintf(out, "AL = (AL + AH * %s) & 0xFF; // AAD\n", arg1);
    } else if (strcmp(op, "aam") == 0) {
        fprintf(out, "AH = AL / %s; AL %%= %s; // AAM\n", arg1, arg1);
    } else if (strcmp(op, "fimul") == 0) {
        fprintf(out, "// fimul: Integer Multiply on FPU (no direct C equivalent)\n");

    /* ---- Default ---- */
    } else {
        fprintf(out, "// unmapped instruction: %s\n", instruction);
    }
}

void convert_to_c(const char *log_path, const char *output_path) {
    if (!log_path || !output_path) {
        fprintf(stderr, "convert_to_c: null path\n");
        return;
    }

    FILE *in = fopen(log_path, "r");
    if (!in) {
        fprintf(stderr, "Error opening assembly log: %s\n", log_path);
        return;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error creating output file: %s\n", output_path);
        fclose(in);
        return;
    }

    write_preamble(out);

    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (len > 1 && line[len - 2] == '\r')
            line[len - 2] = '\0';

        if (line[0] == '\0')
            continue;

        process_instruction(line, out);
    }

    fclose(in);
    fclose(out);

    printf("Output file generated: %s\n", output_path);
}
