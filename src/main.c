/*
 * psretrox - PS2 Reverse Engineering Toolkit
 *
 * Integrated CLI: ISO → ELF extraction → R5900 decode → C translation.
 * Also supports asset extraction, disassembly listing, and ISO browsing.
 *
 * Pure C. No exceptions. No classes. No bullshit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../include/iso_reader.h"
#include "../include/file_utils.h"
#include "../include/assets.h"
#include "../include/r5900_decompiler.h"
#include "../include/recompiler.h"

/* ==================== Logo ==================== */

static void display_logo(void) {
    const char *red   = "\033[31m";
    const char *reset = "\033[0m";

    printf("%s", red);
    printf("\n"
           "                         _                       ___    __ \n"
           "                        | |                     / _ \\  /_ |\n"
           "  _ __   ___  _ __  ___ | |_  _ __  ___ __  __ | | | |  | |\n"
           " | '_ \\ / __|| '__|/ _ \\| __|| '__|/ _ \\\\ \\/ / | | | |  | |\n"
           " | |_) |\\__ \\| |  |  __/| |_ | |  | (_) |>  <  | |_| |_ | |\n"
           " | .__/ |___/|_|   \\___| \\__||_|   \\___//_/\\_\\  \\___/(_)|_|\n"
           " | |                                                       \n"
           " |_|                       \n\n");
    printf("%s", reset);
}

/* ==================== ELF helpers ==================== */

/**
 * Extract the ELF executable name from SYSTEM.CNF inside the ISO.
 * Parses "BOOT2 = cdrom0:\\SLUS_123.45;1" into "SLUS_123.45".
 */
static int get_elf_name_from_system_cnf(struct iso_reader *reader,
                                        char *elf_name, size_t sz)
{
    char cnf[2048];
    if (!iso_reader_find_system_cnf(reader, cnf, sizeof(cnf))) return 0;

    char *boot = strstr(cnf, "BOOT2");
    if (!boot) boot = strstr(cnf, "BOOT");
    if (!boot) return 0;

    char *eq = strchr(boot, '=');
    if (!eq) return 0;
    eq++;
    while (*eq == ' ' || *eq == '\t') eq++;

    char *end = strchr(eq, '\n');
    if (!end) end = eq + strlen(eq);

    size_t len = (size_t)(end - eq);
    if (len >= sz) len = sz - 1;
    strncpy(elf_name, eq, len);
    elf_name[len] = '\0';

    /* strip "cdrom0:\" prefix */
    char *colon = strchr(elf_name, ':');
    if (colon) memmove(elf_name, colon + 1, strlen(colon + 1) + 1);

    /* strip ";1" suffix */
    char *semi = strchr(elf_name, ';');
    if (semi) *semi = '\0';

    /* normalise path separators */
    for (char *p = elf_name; *p; ++p)
        if (*p == '\\') *p = '/';

    /* trim leading whitespace */
    char *start = elf_name;
    while (*start == ' ') start++;
    if (start != elf_name)
        memmove(elf_name, start, strlen(start) + 1);

    /* remove leading '/' — the ISO directory uses bare filenames */
    if (elf_name[0] == '/') {
        memmove(elf_name, elf_name + 1, strlen(elf_name + 1) + 1);
    }

    return 1;
}

/**
 * Read the first PT_LOAD segment from a MIPS ELF32 file.
 * Returns the raw code buffer, its size, and its virtual address.
 */
static int extract_elf_text(const char *elf_path,
                            uint8_t **out_buf, size_t *out_size,
                            uint32_t *out_vaddr)
{
    FILE *f = fopen(elf_path, "rb");
    if (!f) return 0;

    uint8_t e_ident[16];
    if (fread(e_ident, 1, 16, f) != 16) { fclose(f); return 0; }
    if (e_ident[0] != 0x7F || e_ident[1] != 'E' ||
        e_ident[2] != 'L'  || e_ident[3] != 'F') { fclose(f); return 0; }

    /* ELF32 header */
    uint8_t header[52];
    fseek(f, 0, SEEK_SET);
    if (fread(header, 1, 52, f) != 52) { fclose(f); return 0; }

    uint16_t phnum;  memcpy(&phnum, header + 44, 2);
    uint32_t phoff;  memcpy(&phoff, header + 28, 4);

    fseek(f, (long)phoff, SEEK_SET);
    for (int i = 0; i < phnum; ++i) {
        uint8_t ph[32];
        if (fread(ph, 1, 32, f) != 32) break;

        uint32_t type;   memcpy(&type,   ph + 0,  4);
        if (type != 1) continue;  /* PT_LOAD */

        uint32_t offset; memcpy(&offset, ph + 4,  4);
        uint32_t vaddr;  memcpy(&vaddr,  ph + 8,  4);
        uint32_t filesz; memcpy(&filesz, ph + 16, 4);

        *out_buf = (uint8_t *)malloc(filesz);
        if (!*out_buf) { fclose(f); return 0; }

        fseek(f, (long)offset, SEEK_SET);
        if (fread(*out_buf, 1, filesz, f) != filesz) {
            free(*out_buf); *out_buf = NULL;
            fclose(f); return 0;
        }
        *out_size  = filesz;
        *out_vaddr = vaddr;
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/* ==================== Pipeline: ISO → ELF → decode → C ==================== */

/**
 * Full pipeline: open ISO, find ELF via SYSTEM.CNF, extract .text,
 * decode R5900 instructions, translate to C, and write output file.
 */
static int run_pipeline(const char *iso_path)
{
    struct iso_reader reader;
    if (!iso_reader_init(&reader, iso_path)) {
        fprintf(stderr, "Erro ao abrir ISO: %s\n", iso_path);
        return 1;
    }

    /* 1. Find ELF name from SYSTEM.CNF */
    char elf_name[256];
    if (!get_elf_name_from_system_cnf(&reader, elf_name, sizeof(elf_name))) {
        fprintf(stderr, "Nao foi possivel localizar o ELF principal na ISO.\n");
        iso_reader_close(&reader);
        return 1;
    }
    printf("ELF encontrado: %s\n", elf_name);

    /* 2. Extract ELF from ISO */
    const char *extracted_elf = "extracted.elf";
    if (!iso_reader_extract_file_by_name(&reader, elf_name, extracted_elf)) {
        fprintf(stderr, "Falha ao extrair ELF da ISO.\n");
        iso_reader_close(&reader);
        return 1;
    }
    iso_reader_close(&reader);
    printf("ELF extraido: %s\n", extracted_elf);

    /* 3. Read the first PT_LOAD from the ELF */
    uint8_t *text_buf = NULL;
    size_t   text_size = 0;
    uint32_t vaddr = 0;
    if (!extract_elf_text(extracted_elf, &text_buf, &text_size, &vaddr)) {
        fprintf(stderr, "Falha ao extrair segmento .text do ELF.\n");
        return 1;
    }
    printf("Segmento .text: %zu bytes em 0x%08X\n", text_size, vaddr);

    /* 4. Decode R5900 instructions */
    size_t inst_count = text_size / 4;
    r5900_inst_t *insts = (r5900_inst_t *)calloc(inst_count, sizeof(r5900_inst_t));
    if (!insts) {
        fprintf(stderr, "Falha de alocacao para %zu instrucoes.\n", inst_count);
        free(text_buf);
        return 1;
    }
    size_t decoded = 0;
    r5900_decode_buffer(text_buf, text_size, vaddr, insts, &decoded);
    printf("Instrucoes decodificadas: %zu\n", decoded);

    /* 5. Generate disassembly listing */
    r5900_disasm_to_file(text_buf, text_size, vaddr, "disasm_output.asm");
    printf("Disassembly salvo: disasm_output.asm\n");

    /* 6. Translate to C */
    recomp_translate_to_file(insts, decoded, "recompiled_main", "recompiled_output.c");
    printf("Traducao concluida: recompiled_output.c\n");

    free(insts);
    free(text_buf);
    return 0;
}

/* ==================== Helpers (kept from original) ==================== */

/**
 * Convert PSS to MP4 using ffmpeg (external tool).
 */
static int convert_pss_to_mp4(const char *input, const char *output) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -i \"%s\" -vcodec libx264 -acodec aac \"%s\" -y 2>/dev/null",
             input, output);
    int r = system(cmd);
    if (r != 0)
        fprintf(stderr, "ffmpeg conversion failed for: %s\n", input);
    return r;
}

/* ==================== ISO Info ==================== */

static void print_file_cb(const char *filename, void *userdata) {
    int *count = (int *)userdata;
    (*count)++;
    printf("  [%03d] %s\n", *count, filename);
}

static void iso_info(const char *iso_path) {
    struct iso_reader reader;

    if (!iso_reader_init(&reader, iso_path)) {
        fprintf(stderr, "Error opening ISO: %s\n", iso_path);
        return;
    }

    char cnf[512];
    if (iso_reader_find_system_cnf(&reader, cnf, sizeof(cnf)))
        printf("\nSYSTEM.CNF:\n%s\n", cnf);

    printf("\nRoot directory:\n");
    int count = 0;
    iso_reader_read_directory(&reader, print_file_cb, &count);
    printf("Total files: %d\n", count);

    iso_reader_close(&reader);
}

/* ==================== Menu ==================== */

static void menu(void) {
    printf("1 - Pipeline completo (ISO -> ELF -> decode -> C)\n");
    printf("2 - Disassembly R5900 (ISO -> .asm)\n");
    printf("3 - Apenas traduzir para C (ISO -> .c)\n");
    printf("4 - Extract 3D models\n");
    printf("5 - Convert cutscenes (PSS -> MP4)\n");
    printf("6 - Extract audio tracks\n");
    printf("7 - Open ISO and list files\n");
    printf("q - Quit\n\n");
}

/* ==================== Main ==================== */

int main(int argc, char **argv) {

    /* Direct mode: psretrox arquivo.iso */
    if (argc >= 2) {
        display_logo();
        printf("Modo direto: %s\n\n", argv[1]);
        return run_pipeline(argv[1]);
    }

    /* Interactive mode */
    display_logo();

    char selection;

    for (;;) {
        menu();
        printf("> ");
        if (scanf(" %c", &selection) != 1)
            break;

        switch (selection) {

        /* 1: Full pipeline ISO → ELF → decode → C */
        case '1': {
            char iso_path[512];
            printf("ISO path: ");
            scanf(" %511s", iso_path);
            run_pipeline(iso_path);
            break;
        }

        /* 2: Disassembly only */
        case '2': {
            char iso_path[512];
            printf("ISO path: ");
            scanf(" %511s", iso_path);

            struct iso_reader reader;
            if (!iso_reader_init(&reader, iso_path)) {
                fprintf(stderr, "Erro ao abrir ISO: %s\n", iso_path);
                break;
            }
            char elf_name[256];
            if (!get_elf_name_from_system_cnf(&reader, elf_name, sizeof(elf_name))) {
                fprintf(stderr, "ELF nao encontrado.\n");
                iso_reader_close(&reader);
                break;
            }
            iso_reader_extract_file_by_name(&reader, elf_name, "extracted.elf");
            iso_reader_close(&reader);

            uint8_t *buf = NULL; size_t sz = 0; uint32_t va = 0;
            if (extract_elf_text("extracted.elf", &buf, &sz, &va)) {
                r5900_disasm_to_file(buf, sz, va, "disasm_output.asm");
                printf("Disassembly salvo: disasm_output.asm\n");
                free(buf);
            } else {
                fprintf(stderr, "Falha ao extrair .text\n");
            }
            break;
        }

        /* 3: Translate to C only */
        case '3': {
            char iso_path[512];
            printf("ISO path: ");
            scanf(" %511s", iso_path);
            run_pipeline(iso_path);
            break;
        }

        /* 4: 3D models */
        case '4': {
            char bh[512], bd[512], out_dir[512];
            printf("Header file path (e.g. CRASH.BH): ");
            scanf(" %511s", bh);
            printf("Data file path (e.g. CRASH.BD): ");
            scanf(" %511s", bd);
            printf("Output directory: ");
            scanf(" %511s", out_dir);
            extract_models(bh, bd, out_dir);
            break;
        }

        /* 5: PSS → MP4 */
        case '5': {
            char fmv_dir[512], out_dir[512];
            printf("FMV directory (with .PSS files): ");
            scanf(" %511s", fmv_dir);
            printf("Output directory for .mp4: ");
            scanf(" %511s", out_dir);

            create_directories(out_dir);

            DIR *d = opendir(fmv_dir);
            if (!d) { fprintf(stderr, "Cannot open: %s\n", fmv_dir); break; }
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                size_t nlen = strlen(ent->d_name);
                if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".PSS") == 0) {
                    char in_path[1024], out_path[1024], stem[256];
                    strncpy(stem, ent->d_name, nlen - 4);
                    stem[nlen - 4] = '\0';
                    snprintf(in_path, sizeof(in_path), "%s/%s", fmv_dir, ent->d_name);
                    snprintf(out_path, sizeof(out_path), "%s/%s.mp4", out_dir, stem);
                    printf("Converting %s -> %s.mp4\n", ent->d_name, stem);
                    convert_pss_to_mp4(in_path, out_path);
                }
            }
            closedir(d);
            break;
        }

        /* 6: Audio */
        case '6': {
            char mb[512], mh[512], out_dir[512];
            printf("Music data file (e.g. MUSIC.MB): ");
            scanf(" %511s", mb);
            printf("Music header file (e.g. MUSIC.MH): ");
            scanf(" %511s", mh);
            printf("Output directory: ");
            scanf(" %511s", out_dir);
            extract_audio_tracks(mb, mh, out_dir);
            break;
        }

        /* 7: ISO info */
        case '7': {
            char iso_path[512];
            printf("ISO path: ");
            scanf(" %511s", iso_path);
            iso_info(iso_path);
            break;
        }

        case 'q':
        case 'Q':
            printf("Done.\n");
            return 0;

        default:
            printf("Invalid option.\n");
            break;
        }

        printf("\n");
    }

    return 0;
}
