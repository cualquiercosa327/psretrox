#include "../include/assets.h"
#include "../include/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ==================== 3D Model Extraction ==================== */

void extract_models(const char *bh_path, const char *bd_path, const char *output_dir) {
    printf("Initializing model extraction...\n");

    size_t bh_size = 0;
    uint8_t *crash_bh = read_binary_file(bh_path, &bh_size);
    if (!crash_bh) {
        fprintf(stderr, "Error reading header file: %s\n", bh_path);
        return;
    }

    size_t bd_size = 0;
    uint8_t *crash_bd = read_binary_file(bd_path, &bd_size);
    if (!crash_bd) {
        fprintf(stderr, "Error reading data file: %s\n", bd_path);
        free(crash_bh);
        return;
    }

    if (create_directories(output_dir) != 0) {
        /* directory may already exist, not fatal */
    }

    size_t entry_size = 12; /* offset(4) + size(4) + type(4) */
    size_t num_entries = bh_size / entry_size;

    printf("Number of entries: %zu\n", num_entries);

    for (size_t i = 0; i < num_entries; ++i) {
        size_t base = i * entry_size;

        uint32_t offset = read_u32_le(crash_bh + base);
        uint32_t size   = read_u32_le(crash_bh + base + 4);
        uint32_t type   = read_u32_le(crash_bh + base + 8);

        if (offset > bd_size || size > bd_size || offset + size > bd_size) {
            fprintf(stderr,
                    "Error: Block %zu has invalid values. Offset: %u, Size: %u\n",
                    i + 1, offset, size);
            continue;
        }

        printf("Extracting block %zu at offset: 0x%X, size: %u bytes, type: %u\n",
               i + 1, offset, size, type);

        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/block_%zu.bin", output_dir, i + 1);

        if (write_binary_file(output_path, crash_bd + offset, size) != 0) {
            fprintf(stderr, "Error writing block %zu to %s\n", i + 1, output_path);
            continue;
        }

        printf("Block %zu saved to: %s\n", i + 1, output_path);
    }

    printf("Model extraction completed.\n");

    free(crash_bh);
    free(crash_bd);
}

/* ==================== Audio Track Extraction ==================== */

void extract_audio_tracks(const char *mb_path, const char *mh_path, const char *output_dir) {
    printf("Initializing audio extraction...\n");

    size_t mb_size = 0;
    uint8_t *music_mb = read_binary_file(mb_path, &mb_size);
    if (!music_mb) {
        fprintf(stderr, "Error reading music data file: %s\n", mb_path);
        return;
    }

    size_t mh_size = 0;
    uint8_t *music_mh = read_binary_file(mh_path, &mh_size);
    if (!music_mh) {
        fprintf(stderr, "Error reading music header file: %s\n", mh_path);
        free(music_mb);
        return;
    }

    if (create_directories(output_dir) != 0) {
        /* directory may already exist, not fatal */
    }

    size_t entry_size = 8; /* offset(4) + size(4) */
    size_t num_tracks = mh_size / entry_size;

    printf("Number of tracks: %zu\n", num_tracks);

    for (size_t i = 0; i < num_tracks; ++i) {
        size_t base = i * entry_size;

        uint32_t offset = read_u32_le(music_mh + base);
        uint32_t size   = read_u32_le(music_mh + base + 4);

        printf("Extracting track %zu at offset: 0x%X, size: %u bytes\n",
               i + 1, offset, size);

        if ((size_t)offset + size > mb_size) {
            fprintf(stderr, "Error: Track %zu exceeds data file bounds.\n", i + 1);
            continue;
        }

        if (size == 0) {
            fprintf(stderr, "Warning: Track %zu has zero size, skipping.\n", i + 1);
            continue;
        }

        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/track_%zu.vag", output_dir, i + 1);

        if (write_binary_file(output_path, music_mb + offset, size) != 0) {
            fprintf(stderr, "Error writing track %zu to %s\n", i + 1, output_path);
            continue;
        }

        printf("Track %zu saved to: %s\n", i + 1, output_path);
    }

    printf("Audio extraction completed.\n");

    free(music_mb);
    free(music_mh);
}
