#ifndef ISO_READER_H
#define ISO_READER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define ISO_BLOCK_SIZE 2048
#define ISO_MAX_PATH 512
#define ISO_MAX_EXTENTS 64

/* ==================== Core Structures ==================== */

struct iso_reader {
    FILE *file;
    int last_error;          /* Last error code (0 = no error) */
    char error_msg[256];     /* Last error message */
};

enum iso_format { 
    ISO_FORMAT_9660, 
    ISO_FORMAT_UDF, 
    ISO_FORMAT_UNKNOWN 
};

enum iso_error {
    ISO_OK = 0,
    ISO_ERR_NULL_POINTER,
    ISO_ERR_FILE_NOT_OPEN,
    ISO_ERR_READ_FAILED,
    ISO_ERR_SEEK_FAILED,
    ISO_ERR_FILE_NOT_FOUND,
    ISO_ERR_INVALID_FORMAT,
    ISO_ERR_CHECKSUM_MISMATCH,
    ISO_ERR_BUFFER_TOO_SMALL,
    ISO_ERR_WRITE_FAILED
};

/**
 * Represents a single extent (fragment) of a file.
 * 
 * In ISO 9660, large files (>4GB) or files that couldn't be stored
 * contiguously on the disc are split into multiple extents.
 * Each extent has its own LBA (starting block) and size.
 */
struct iso_extent {
    uint32_t lba;            /* Logical Block Address (starting block) */
    uint32_t size;           /* Size in bytes of this extent */
};

/**
 * File entry with support for multiple extents (fragmented files).
 * 
 * A single file may span multiple non-contiguous regions on the disc.
 * This structure collects all extents so they can be read in order.
 */
struct iso_file_entry {
    char name[256];
    uint8_t is_directory;
    uint32_t total_size;              /* Sum of all extent sizes */
    size_t extent_count;              /* Number of extents (usually 1) */
    struct iso_extent extents[ISO_MAX_EXTENTS];
};

/* Directory entry for recursive traversal */
struct iso_dir_entry {
    char path[ISO_MAX_PATH];
    uint32_t lba;
    uint32_t size;
};

/* ==================== Core Functions ==================== */

/**
 * @brief Initializes the ISO reader. Returns 1 on success, 0 on failure.
 */
int iso_reader_init(struct iso_reader *reader, const char *path);

/**
 * @brief Closes the ISO reader and releases resources.
 */
void iso_reader_close(struct iso_reader *reader);

/**
 * @brief Checks if the ISO file is open. Returns 1 if open, 0 if not.
 */
int iso_reader_is_open(const struct iso_reader *reader);

/**
 * @brief Reads a block of data from the ISO. Returns number of bytes read.
 */
size_t iso_reader_read_block(struct iso_reader *reader, size_t block_index, uint8_t *buffer, size_t block_size);

/**
 * @brief Reads a range of bytes from the ISO. Returns number of bytes read.
 */
size_t iso_reader_read_range(struct iso_reader *reader, size_t offset, size_t length, uint8_t *buffer);

/**
 * @brief Searches and returns the content of SYSTEM.CNF if found.
 * @return 1 if found, 0 if not
 */
int iso_reader_find_system_cnf(struct iso_reader *reader, char *buffer, size_t bufsize);

/**
 * @brief Reads and lists files in the root directory of the ISO.
 */
void iso_reader_read_directory(struct iso_reader *reader, void (*on_file)(const char *filename, void *userdata), void *userdata);

/**
 * @brief Recursively reads and lists all files starting from the given path.
 */
void iso_reader_read_directory_recursive(struct iso_reader *reader, const char *path, void (*on_file)(const char*, void*), void *userdata);

/**
 * @brief Extracts a file from the ISO by name (simple, single-extent only).
 * @return 1 on success, 0 on failure
 */
int iso_reader_extract_file_by_name(struct iso_reader *reader, const char *iso_filename, const char *output_path);

/**
 * @brief Detects the format of the ISO image.
 * @return enum iso_format value
 */
int iso_reader_detect_format(struct iso_reader *reader);

/* ==================== Fragmented File Support ==================== */

/**
 * @brief Finds a file entry including all extents (for fragmented files).
 * @return 1 on success, 0 on failure
 */
int iso_reader_find_file_entry(struct iso_reader *reader, const char *iso_filename, struct iso_file_entry *entry);

/**
 * @brief Extracts a file using its file entry (supports fragmented files).
 * @return 1 on success, 0 on failure
 */
int iso_reader_extract_file_entry(struct iso_reader *reader, const struct iso_file_entry *entry, const char *output_path);

/* ==================== Validation & Integrity ==================== */

/**
 * @brief Calculates CRC32 checksum of a buffer.
 */
uint32_t iso_reader_calculate_crc32(const uint8_t *data, size_t size);

/**
 * @brief Validates a file from the ISO by calculating its CRC32.
 * @return 1 on success, 0 on failure
 */
int iso_reader_validate_file(struct iso_reader *reader, const char *iso_filename, uint32_t *out_crc);

/**
 * @brief Verifies file integrity comparing against expected CRC32.
 * @return 1 if checksums match, 0 otherwise
 */
int iso_reader_verify_file_crc(struct iso_reader *reader, const char *iso_filename, uint32_t expected_crc);

/* ==================== Error Handling ==================== */

/**
 * @brief Gets the last error code.
 */
int iso_reader_get_last_error(const struct iso_reader *reader);

/**
 * @brief Gets the last error message.
 */
const char* iso_reader_get_last_error_msg(const struct iso_reader *reader);

/**
 * @brief Clears the last error.
 */
void iso_reader_clear_error(struct iso_reader *reader);

#endif /* ISO_READER_H */