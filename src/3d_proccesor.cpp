#include "3d_proccesor.h"
#include "../include/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// C version of the function to extract 3D models from CRASH.BH and CRASH.BD files
void extract_models(const char* bh_path, const char* bd_path, const char* output_dir) {
  printf("Inicializing model extraction...\n");
  size_t bh_size = 0;
  uint8_t* crash_bh = read_binary_file(bh_path, &bh_size);
  if (!crash_bh) {
    fprintf(stderr, "Error reading CRASH.BH file\n");
    free(crash_bh);
    return;
  }

  size_t bd_size = 0;
  uint8_t* crash_bd = read_binary_file(bd_path, &bd_size);
  if (!crash_bd) {
    fprintf(stderr, "Error reading CRASH.BD file\n");
    free(crash_bh);
    return;
  }

  /* Create output directory (equivalent to std::filesystem::create_directories) */
  if (create_directories(output_dir) != 0) {
    perror("Error creating output directory");
    free(crash_bh);
    free(crash_bd);
    return;
  }

  size_t entry_size = 12; /* offset (4) + size (4) + type (4) */
  size_t num_entries = bh_size / entry_size;

  printf("Number of entries: %zu\n", num_entries);

  for (size_t i = 0; i < num_entries; ++i) {
    size_t base = i * entry_size;

    uint32_t offset = read_u32_le(crash_bh + base);
    uint32_t size   = read_u32_le(crash_bh + base + 4);
    uint32_t type   = read_u32_le(crash_bh + base + 8);

    /* Validate values */
    if (offset > bd_size || size > bd_size || offset + size > bd_size) {
      fprintf(stderr,
              "Error: Block %zu has invalid values. Offset: %u, Size: %u\n",
              i + 1, offset, size);
      continue;
    }

    printf(
      "Extracting block %zu at offset: 0x%X with size: %u bytes, type: %u\n",
      i + 1, offset, size, type
    );

    /* Build output filename */
    char output_path[1024];
    snprintf(
      output_path,
      sizeof(output_path),
      "%s/block_%zu.bin",
      output_dir,
      i + 1
    );

    /* Write block data directly from CRASH.BD */
    if (write_binary_file(output_path, crash_bd + offset, size) != 0) {
      fprintf(stderr,
              "Error writing block %zu to file %s\n",
              i + 1, output_path);
      continue;
    }

    printf("Block %zu saved to: %s\n", i + 1, output_path);
  }

  printf("Model extraction completed.\n");

  free(crash_bh);
  free(crash_bd);
}

