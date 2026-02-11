/*
 * psretrox - PS2 Reverse Engineering Toolkit
 *
 * Integrated CLI that orchestrates ISO reading, asset extraction,
 * disassembly, and asm-to-C conversion.
 *
 * Pure C. No exceptions. No classes. No bullshit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../include/iso_reader.h"
#include "../include/file_utils.h"
#include "../include/assets.h"
#include "../include/disasm.h"
#include "../include/convert.h"

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

/* ==================== Menu ==================== */

static void menu(void) {
    printf("1 - Decompile files and recompile to C\n");
    printf("2 - Decompile to MIPS assembly\n");
    printf("3 - Recompile assembly to C\n");
    printf("4 - Extract 3D models\n");
    printf("5 - Convert cutscenes (PSS → MP4)\n");
    printf("6 - Extract audio tracks\n");
    printf("7 - Open ISO and list files\n");
    printf("q - Quit\n\n");
}

/* ==================== Helpers ==================== */

/**
 * Disassemble a single file by path.
 * Reads binary, calls disassemble_code, frees buffer.
 */
static void disasm_file(const char *path, const char *label) {
    size_t size = 0;
    uint8_t *data = read_binary_file(path, &size);
    if (!data) {
        fprintf(stderr, "Could not read: %s\n", path);
        return;
    }
    printf("Disassembling %s...\n", label);
    disassemble_code(data, size, label);
    free(data);
}

/**
 * Iterate a directory, disassemble all files matching a given extension.
 */
static void disasm_directory(const char *dir_path, const char *extension) {
    DIR *d = opendir(dir_path);
    if (!d) {
        fprintf(stderr, "Cannot open directory: %s\n", dir_path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        size_t elen = strlen(extension);
        if (nlen > elen && strcmp(ent->d_name + nlen - elen, extension) == 0) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
            disasm_file(full_path, ent->d_name);
        }
    }
    closedir(d);
}

/**
 * Convert PSS to MP4 using ffmpeg (external tool).
 */
static int convert_pss_to_mp4(const char *input, const char *output) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "ffmpeg -i \"%s\" -vcodec libx264 -acodec aac \"%s\" -y 2>/dev/null", input, output);
    int r = system(cmd);
    if (r != 0) {
        fprintf(stderr, "ffmpeg conversion failed for: %s\n", input);
    }
    return r;
}

/**
 * Recompile all .asm files in a directory to C.
 */
static void recomp_directory(const char *asm_dir, const char *c_dir) {
    create_directories(c_dir);

    DIR *d = opendir(asm_dir);
    if (!d) {
        fprintf(stderr, "Cannot open directory: %s\n", asm_dir);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".asm") == 0) {
            char asm_path[1024], c_path[1024], stem[256];

            strncpy(stem, ent->d_name, nlen - 4);
            stem[nlen - 4] = '\0';

            snprintf(asm_path, sizeof(asm_path), "%s/%s", asm_dir, ent->d_name);
            snprintf(c_path, sizeof(c_path), "%s/%s.c", c_dir, stem);

            printf("Converting %s → %s.c\n", ent->d_name, stem);
            convert_to_c(asm_path, c_path);
        }
    }
    closedir(d);
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

    /* SYSTEM.CNF */
    char cnf[512];
    if (iso_reader_find_system_cnf(&reader, cnf, sizeof(cnf))) {
        printf("\nSYSTEM.CNF:\n%s\n", cnf);
    }

    /* Root directory listing */
    printf("\nRoot directory:\n");
    int count = 0;
    iso_reader_read_directory(&reader, print_file_cb, &count);
    printf("Total files: %d\n", count);

    iso_reader_close(&reader);
}

/* ==================== Main ==================== */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    display_logo();

    char selection;

    for (;;) {
        menu();
        printf("> ");
        if (scanf(" %c", &selection) != 1)
            break;

        switch (selection) {

        /* 1: Full pipeline */
        case '1': {
            char iso_path[512];
            printf("ISO path: ");
            scanf(" %511s", iso_path);

            /* TODO: extract ISO contents to iso/ then run pipeline */
            printf("Full pipeline not yet wired — use individual options.\n");
            break;
        }

        /* 2: Disassemble */
        case '2': {
            char path[512];
            printf("File or directory to disassemble: ");
            scanf(" %511s", path);

            struct stat st;
            if (stat(path, &st) != 0) {
                fprintf(stderr, "Path not found: %s\n", path);
                break;
            }

            if (S_ISDIR(st.st_mode)) {
                disasm_directory(path, ".BIN");
                disasm_directory(path, ".IRX");
            } else {
                disasm_file(path, path);
            }
            break;
        }

        /* 3: Recompile asm → C */
        case '3': {
            char asm_dir[512], c_dir[512];
            printf("Assembly directory: ");
            scanf(" %511s", asm_dir);
            printf("C output directory: ");
            scanf(" %511s", c_dir);

            recomp_directory(asm_dir, c_dir);
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
            if (!d) {
                fprintf(stderr, "Cannot open: %s\n", fmv_dir);
                break;
            }
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                size_t nlen = strlen(ent->d_name);
                if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".PSS") == 0) {
                    char in_path[1024], out_path[1024], stem[256];
                    strncpy(stem, ent->d_name, nlen - 4);
                    stem[nlen - 4] = '\0';

                    snprintf(in_path, sizeof(in_path), "%s/%s", fmv_dir, ent->d_name);
                    snprintf(out_path, sizeof(out_path), "%s/%s.mp4", out_dir, stem);

                    printf("Converting %s → %s.mp4\n", ent->d_name, stem);
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
