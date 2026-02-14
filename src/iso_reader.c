#include "../include/iso_reader.h"
#include "../include/file_utils.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ==================== Internal Helper Functions ==================== */

static void set_error(struct iso_reader *reader, int code, const char *msg) {
    if (reader) {
        reader->last_error = code;
        if (msg) {
            strncpy(reader->error_msg, msg, sizeof(reader->error_msg) - 1);
            reader->error_msg[sizeof(reader->error_msg) - 1] = '\0';
        } else {
            reader->error_msg[0] = '\0';
        }
    }
}

/* CRC32 lookup table (IEEE polynomial) */
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void init_crc32_table(void) {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

/* Helper: Read directory LBA and size from PVD */
static int get_root_directory_info(struct iso_reader *reader, uint32_t *lba, uint32_t *size) {
    uint8_t block[ISO_BLOCK_SIZE];
    if (iso_reader_read_block(reader, 16, block, ISO_BLOCK_SIZE) == 0)
        return 0;
    
    size_t offset = 156;
    *lba = block[offset + 2] | (block[offset + 3] << 8) |
           (block[offset + 4] << 16) | (block[offset + 5] << 24);
    *size = block[offset + 10] | (block[offset + 11] << 8) |
            (block[offset + 12] << 16) | (block[offset + 13] << 24);
    return 1;
}

/* Helper: Find directory by path and return its LBA and size */
static int find_directory_by_path(struct iso_reader *reader, const char *path, uint32_t *out_lba, uint32_t *out_size) {
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return get_root_directory_info(reader, out_lba, out_size);
    }

    uint32_t current_lba, current_size;
    if (!get_root_directory_info(reader, &current_lba, &current_size))
        return 0;

    char path_copy[ISO_MAX_PATH];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = strtok(path_copy, "/\\");
    while (token) {
        char search_name[256];
        strncpy(search_name, token, sizeof(search_name) - 1);
        search_name[sizeof(search_name) - 1] = '\0';
        str_to_upper(search_name);

        int found = 0;
        size_t num_blocks = (current_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

        for (size_t i = 0; i < num_blocks && !found; ++i) {
            uint8_t dir_block[ISO_BLOCK_SIZE];
            if (iso_reader_read_block(reader, current_lba + i, dir_block, ISO_BLOCK_SIZE) == 0)
                continue;

            size_t pos = 0;
            while (pos < ISO_BLOCK_SIZE) {
                uint8_t record_len = dir_block[pos];
                if (record_len == 0) break;

                uint8_t flags = dir_block[pos + 25];
                uint8_t name_len = dir_block[pos + 32];

                if (name_len > 0 && pos + 33 + name_len <= ISO_BLOCK_SIZE) {
                    char raw_name[256], clean_name[256];
                    memcpy(raw_name, &dir_block[pos + 33], name_len);
                    raw_name[name_len] = '\0';

                    if (!(name_len == 1 && (raw_name[0] == '\0' || raw_name[0] == '\1'))) {
                        clean_iso_filename_buffer(raw_name, name_len, clean_name, sizeof(clean_name));
                        str_to_upper(clean_name);

                        if (strcmp(clean_name, search_name) == 0 && (flags & 0x02)) {
                            current_lba = dir_block[pos + 2] | (dir_block[pos + 3] << 8) |
                                          (dir_block[pos + 4] << 16) | (dir_block[pos + 5] << 24);
                            current_size = dir_block[pos + 10] | (dir_block[pos + 11] << 8) |
                                           (dir_block[pos + 12] << 16) | (dir_block[pos + 13] << 24);
                            found = 1;
                            break;
                        }
                    }
                }
                pos += record_len;
            }
        }

        if (!found) return 0;
        token = strtok(NULL, "/\\");
    }

    *out_lba = current_lba;
    *out_size = current_size;
    return 1;
}

/* ==================== Core Functions ==================== */

int iso_reader_init(struct iso_reader *reader, const char *path) {
    if (!reader || !path) {
        if (reader) set_error(reader, ISO_ERR_NULL_POINTER, "Null pointer passed to init");
        return 0;
    }

    reader->last_error = ISO_OK;
    reader->error_msg[0] = '\0';
    reader->file = fopen(path, "rb");
    
    if (!reader->file) {
        set_error(reader, ISO_ERR_FILE_NOT_OPEN, "Failed to open ISO file");
        return 0;
    }
    return 1;
}

void iso_reader_close(struct iso_reader *reader) {
    if (reader && reader->file) {
        fclose(reader->file);
        reader->file = NULL;
    }
}

int iso_reader_is_open(const struct iso_reader *reader) {
    return reader && reader->file != NULL;
}

size_t iso_reader_read_block(struct iso_reader *reader, size_t block_index, uint8_t *buffer, size_t block_size) {
    if (!reader || !reader->file || !buffer)
        return 0;

    long offset = (long)(block_index * block_size);
    if (fseek(reader->file, offset, SEEK_SET) != 0)
        return 0;

    return fread(buffer, 1, block_size, reader->file);
}

size_t iso_reader_read_range(struct iso_reader *reader, size_t offset, size_t length, uint8_t *buffer) {
    if (!reader || !reader->file || !buffer)
        return 0;

    if (fseek(reader->file, (long)offset, SEEK_SET) != 0)
        return 0;

    return fread(buffer, 1, length, reader->file);
}

int iso_reader_find_system_cnf(struct iso_reader *reader, char *buffer, size_t bufsize) {
    if (!reader || !reader->file || !buffer || bufsize == 0)
        return 0;

    
    struct iso_file_entry entry;
    if (!iso_reader_find_file_entry(reader, "SYSTEM.CNF", &entry)) {
        buffer[0] = '\0';
        return 0;
    }

    // Lê o conteúdo do arquivo SYSTEM.CNF para o buffer
    size_t total_to_read = (entry.total_size < bufsize - 1) ? entry.total_size : bufsize - 1;
    size_t total_read = 0;
    size_t ext;
    for (ext = 0; ext < entry.extent_count && total_read < total_to_read; ++ext) {
        uint32_t ext_lba = entry.extents[ext].lba;
        uint32_t ext_size = entry.extents[ext].size;
        size_t block_idx = 0;
        while (ext_size > 0 && total_read < total_to_read) {
            uint8_t data_block[ISO_BLOCK_SIZE];
            size_t bytes_read = iso_reader_read_block(reader, ext_lba + block_idx, data_block, ISO_BLOCK_SIZE);
            if (bytes_read == 0)
                break;
            size_t to_copy = ext_size < ISO_BLOCK_SIZE ? ext_size : ISO_BLOCK_SIZE;
            if (to_copy > total_to_read - total_read)
                to_copy = total_to_read - total_read;
            memcpy(buffer + total_read, data_block, to_copy);
            total_read += to_copy;
            ext_size -= (uint32_t)to_copy;
            ++block_idx;
        }
    }
    buffer[total_read] = '\0';
    return (total_read > 0);
}

void iso_reader_read_directory(struct iso_reader *reader, void (*on_file)(const char *filename, void *userdata), void *userdata) {
    if (!reader || !reader->file || !on_file)
        return;

    uint8_t block[ISO_BLOCK_SIZE];

    /* Read Primary Volume Descriptor (block 16) */
    if (iso_reader_read_block(reader, 16, block, ISO_BLOCK_SIZE) == 0)
        return;

    /* Root Directory Record starts at byte 156 of PVD (ISO 9660) */
    size_t root_dir_offset = 156;

    /* LBA of root directory (little-endian, 4 bytes at offset +2) */
    uint32_t root_dir_lba = block[root_dir_offset + 2]
                          | (block[root_dir_offset + 3] << 8)
                          | (block[root_dir_offset + 4] << 16)
                          | (block[root_dir_offset + 5] << 24);

    /* Size of root directory (little-endian, 4 bytes at offset +10) */
    uint32_t root_dir_size = block[root_dir_offset + 10]
                           | (block[root_dir_offset + 11] << 8)
                           | (block[root_dir_offset + 12] << 16)
                           | (block[root_dir_offset + 13] << 24);

    size_t num_blocks = (root_dir_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

    for (size_t i = 0; i < num_blocks; ++i) {
        uint8_t dir_block[ISO_BLOCK_SIZE];
        if (iso_reader_read_block(reader, root_dir_lba + i, dir_block, ISO_BLOCK_SIZE) == 0)
            continue;

        size_t pos = 0;
        while (pos < ISO_BLOCK_SIZE) {
            uint8_t record_length = dir_block[pos];
            if (record_length == 0)
                break;

            uint8_t name_length = dir_block[pos + 32];
            if (name_length > 0 && pos + 33 + name_length <= ISO_BLOCK_SIZE) {
                char raw_name[256];
                char clean_name[256];

                memcpy(raw_name, &dir_block[pos + 33], name_length);
                raw_name[name_length] = '\0';

                /* Skip special entries '.' and '..' */
                if (!(name_length == 1 && (raw_name[0] == '\0' || raw_name[0] == '\1'))) {
                    clean_iso_filename_buffer(raw_name, name_length, clean_name, sizeof(clean_name));
                    on_file(clean_name, userdata);
                }
            }

            pos += record_length;
        }
    }
}

void iso_reader_read_directory_recursive(struct iso_reader *reader, const char *path, void (*on_file)(const char*, void*), void *userdata) {
    if (!reader || !reader->file || !on_file) {
        set_error(reader, ISO_ERR_NULL_POINTER, "Invalid parameters for recursive directory read");
        return;
    }

    /* Stack-based iterative approach to avoid deep recursion */
    struct iso_dir_entry stack[128];
    int stack_top = 0;

    /* Initialize with starting directory */
    uint32_t start_lba, start_size;
    if (!find_directory_by_path(reader, path, &start_lba, &start_size)) {
        set_error(reader, ISO_ERR_FILE_NOT_FOUND, "Starting path not found");
        return;
    }

    strncpy(stack[0].path, path ? path : "", ISO_MAX_PATH - 1);
    stack[0].path[ISO_MAX_PATH - 1] = '\0';
    stack[0].lba = start_lba;
    stack[0].size = start_size;
    stack_top = 1;

    while (stack_top > 0) {
        struct iso_dir_entry current = stack[--stack_top];
        size_t num_blocks = (current.size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

        for (size_t i = 0; i < num_blocks; ++i) {
            uint8_t dir_block[ISO_BLOCK_SIZE];
            if (iso_reader_read_block(reader, current.lba + i, dir_block, ISO_BLOCK_SIZE) == 0)
                continue;

            size_t pos = 0;
            while (pos < ISO_BLOCK_SIZE) {
                uint8_t record_len = dir_block[pos];
                if (record_len == 0) break;

                uint8_t flags = dir_block[pos + 25];
                uint8_t name_len = dir_block[pos + 32];

                if (name_len > 0 && pos + 33 + name_len <= ISO_BLOCK_SIZE) {
                    char raw_name[256], clean_name[256];
                    memcpy(raw_name, &dir_block[pos + 33], name_len);
                    raw_name[name_len] = '\0';

                    /* Skip . and .. entries */
                    if (!(name_len == 1 && (raw_name[0] == '\0' || raw_name[0] == '\1'))) {
                        clean_iso_filename_buffer(raw_name, name_len, clean_name, sizeof(clean_name));

                        /* Build full path manually to avoid truncation warnings */
                        char full_path[ISO_MAX_PATH];
                        full_path[0] = '\0';
                        
                        if (current.path[0] && strcmp(current.path, "/") != 0) {
                            size_t plen = strlen(current.path);
                            if (plen < ISO_MAX_PATH - 1) {
                                memcpy(full_path, current.path, plen);
                                full_path[plen] = '/';
                                size_t remaining = ISO_MAX_PATH - plen - 2;
                                size_t nlen = strlen(clean_name);
                                if (nlen > remaining) nlen = remaining;
                                memcpy(full_path + plen + 1, clean_name, nlen);
                                full_path[plen + 1 + nlen] = '\0';
                            }
                        } else {
                            full_path[0] = '/';
                            size_t nlen = strlen(clean_name);
                            if (nlen > ISO_MAX_PATH - 2) nlen = ISO_MAX_PATH - 2;
                            memcpy(full_path + 1, clean_name, nlen);
                            full_path[1 + nlen] = '\0';
                        }

                        /* Call callback with full path */
                        on_file(full_path, userdata);

                        /* If directory, push to stack for later processing */
                        if ((flags & 0x02) && stack_top < 128) {
                            uint32_t sub_lba = dir_block[pos + 2] | (dir_block[pos + 3] << 8) |
                                               (dir_block[pos + 4] << 16) | (dir_block[pos + 5] << 24);
                            uint32_t sub_size = dir_block[pos + 10] | (dir_block[pos + 11] << 8) |
                                                (dir_block[pos + 12] << 16) | (dir_block[pos + 13] << 24);

                            size_t fplen = strlen(full_path);
                            if (fplen >= ISO_MAX_PATH) fplen = ISO_MAX_PATH - 1;
                            memcpy(stack[stack_top].path, full_path, fplen);
                            stack[stack_top].path[fplen] = '\0';
                            stack[stack_top].lba = sub_lba;
                            stack[stack_top].size = sub_size;
                            stack_top++;
                        }
                    }
                }
                pos += record_len;
            }
        }
    }
}

int iso_reader_extract_file_by_name(struct iso_reader *reader, const char *iso_filename, const char *output_path) {
    if (!reader || !reader->file || !iso_filename || !output_path)
        return 0;

    uint8_t block[ISO_BLOCK_SIZE];

    /* Normalize search name to uppercase */
    char normalized_search[256];
    strncpy(normalized_search, iso_filename, sizeof(normalized_search) - 1);
    normalized_search[sizeof(normalized_search) - 1] = '\0';
    str_to_upper(normalized_search);

    /* Read Primary Volume Descriptor (block 16) */
    if (iso_reader_read_block(reader, 16, block, ISO_BLOCK_SIZE) == 0)
        return 0;

    size_t root_dir_offset = 156;

    uint32_t root_dir_lba = block[root_dir_offset + 2]
                          | (block[root_dir_offset + 3] << 8)
                          | (block[root_dir_offset + 4] << 16)
                          | (block[root_dir_offset + 5] << 24);

    uint32_t root_dir_size = block[root_dir_offset + 10]
                           | (block[root_dir_offset + 11] << 8)
                           | (block[root_dir_offset + 12] << 16)
                           | (block[root_dir_offset + 13] << 24);

    size_t num_blocks = (root_dir_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

    for (size_t i = 0; i < num_blocks; ++i) {
        uint8_t dir_block[ISO_BLOCK_SIZE];
        if (iso_reader_read_block(reader, root_dir_lba + i, dir_block, ISO_BLOCK_SIZE) == 0)
            continue;

        size_t pos = 0;
        while (pos < ISO_BLOCK_SIZE) {
            uint8_t record_length = dir_block[pos];
            if (record_length == 0)
                break;

            uint8_t name_length = dir_block[pos + 32];
            if (name_length > 0 && pos + 33 + name_length <= ISO_BLOCK_SIZE) {
                char raw_name[256];
                char clean_name[256];

                memcpy(raw_name, &dir_block[pos + 33], name_length);
                raw_name[name_length] = '\0';

                clean_iso_filename_buffer(raw_name, name_length, clean_name, sizeof(clean_name));
                str_to_upper(clean_name);

                if (strcmp(clean_name, normalized_search) == 0) {
                    /* Found the file! */
                    uint32_t file_lba = dir_block[pos + 2]
                                      | (dir_block[pos + 3] << 8)
                                      | (dir_block[pos + 4] << 16)
                                      | (dir_block[pos + 5] << 24);

                    uint32_t file_size = dir_block[pos + 10]
                                       | (dir_block[pos + 11] << 8)
                                       | (dir_block[pos + 12] << 16)
                                       | (dir_block[pos + 13] << 24);

                    /* Open output file */
                    FILE *out_file = fopen(output_path, "wb");
                    if (!out_file)
                        return 0;

                    size_t bytes_remaining = file_size;
                    size_t block_idx = 0;

                    while (bytes_remaining > 0) {
                        uint8_t data_block[ISO_BLOCK_SIZE];
                        size_t bytes_read = iso_reader_read_block(reader, file_lba + block_idx, data_block, ISO_BLOCK_SIZE);

                        if (bytes_read == 0) {
                            fclose(out_file);
                            return 0;
                        }

                        size_t to_write = (bytes_remaining < ISO_BLOCK_SIZE) ? bytes_remaining : ISO_BLOCK_SIZE;
                        fwrite(data_block, 1, to_write, out_file);

                        bytes_remaining -= to_write;
                        ++block_idx;
                    }

                    fclose(out_file);
                    return 1;
                }
            }

            pos += record_length;
        }
    }

    return 0;
}

int iso_reader_detect_format(struct iso_reader *reader) {
    if (!reader || !reader->file) {
        set_error(reader, ISO_ERR_NULL_POINTER, "Invalid reader for format detection");
        return ISO_FORMAT_UNKNOWN;
    }

    uint8_t block[ISO_BLOCK_SIZE];

    /* Check ISO9660 (Primary Volume Descriptor at block 16) */
    if (iso_reader_read_block(reader, 16, block, ISO_BLOCK_SIZE) == ISO_BLOCK_SIZE) {
        if (block[0] == 1 && memcmp(&block[1], "CD001", 5) == 0) {
            return ISO_FORMAT_9660;
        }
    }

    /* Check UDF (Anchor Volume Descriptor Pointer at block 256) */
    if (iso_reader_read_block(reader, 256, block, ISO_BLOCK_SIZE) == ISO_BLOCK_SIZE) {
        if (memcmp(&block[0], "\x01\x00\x00\x00\x00\x00\x00\x00", 8) == 0 &&
            memcmp(&block[8], "BEA01", 5) == 0) {
            return ISO_FORMAT_UDF;
        }
    }

    set_error(reader, ISO_ERR_INVALID_FORMAT, "Unknown ISO format");
    return ISO_FORMAT_UNKNOWN;
}

/* ==================== Fragmented File Support ==================== */

int iso_reader_find_file_entry(struct iso_reader *reader, const char *iso_filename, struct iso_file_entry *entry) {
    if (!reader || !reader->file || !iso_filename || !entry) {
        set_error(reader, ISO_ERR_NULL_POINTER, "Invalid parameters for find_file_entry");
        return 0;
    }

    memset(entry, 0, sizeof(*entry));

    /* Normalize search name to uppercase */
    char normalized_search[256];
    strncpy(normalized_search, iso_filename, sizeof(normalized_search) - 1);
    normalized_search[sizeof(normalized_search) - 1] = '\0';
    str_to_upper(normalized_search);

    uint32_t root_lba, root_size;
    if (!get_root_directory_info(reader, &root_lba, &root_size)) {
        set_error(reader, ISO_ERR_READ_FAILED, "Failed to read root directory info");
        return 0;
    }

    size_t num_blocks = (root_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

    for (size_t i = 0; i < num_blocks; ++i) {
        uint8_t dir_block[ISO_BLOCK_SIZE];
        if (iso_reader_read_block(reader, root_lba + i, dir_block, ISO_BLOCK_SIZE) == 0)
            continue;

        size_t pos = 0;
        while (pos < ISO_BLOCK_SIZE) {
            uint8_t record_len = dir_block[pos];
            if (record_len == 0) break;

            uint8_t flags = dir_block[pos + 25];
            uint8_t name_len = dir_block[pos + 32];

            if (name_len > 0 && pos + 33 + name_len <= ISO_BLOCK_SIZE) {
                char raw_name[256], clean_name[256];
                memcpy(raw_name, &dir_block[pos + 33], name_len);
                raw_name[name_len] = '\0';

                clean_iso_filename_buffer(raw_name, name_len, clean_name, sizeof(clean_name));
                str_to_upper(clean_name);

                if (strcmp(clean_name, normalized_search) == 0) {
                    strncpy(entry->name, clean_name, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                    entry->is_directory = (flags & 0x02) ? 1 : 0;

                    uint32_t file_lba = dir_block[pos + 2] | (dir_block[pos + 3] << 8) |
                                        (dir_block[pos + 4] << 16) | (dir_block[pos + 5] << 24);
                    uint32_t file_size = dir_block[pos + 10] | (dir_block[pos + 11] << 8) |
                                         (dir_block[pos + 12] << 16) | (dir_block[pos + 13] << 24);

                    /* First extent */
                    entry->extents[0].lba = file_lba;
                    entry->extents[0].size = file_size;
                    entry->extent_count = 1;
                    entry->total_size = file_size;

                    /*
                     * Check for multi-extent flag (ISO 9660 Section 6.8.2.4)
                     * Bit 7 (0x80) of flags indicates more extents follow.
                     * This is used for files >4GB or fragmented files.
                     */
                    if (flags & 0x80) {
                        /* Search for continuation entries */
                        size_t search_pos = pos + record_len;
                        while (search_pos < ISO_BLOCK_SIZE && entry->extent_count < ISO_MAX_EXTENTS) {
                            uint8_t next_len = dir_block[search_pos];
                            if (next_len == 0) break;

                            uint8_t next_flags = dir_block[search_pos + 25];
                            uint8_t next_name_len = dir_block[search_pos + 32];

                            /* Continuation has same name */
                            if (next_name_len == name_len) {
                                char next_raw[256], next_clean[256];
                                memcpy(next_raw, &dir_block[search_pos + 33], next_name_len);
                                next_raw[next_name_len] = '\0';
                                clean_iso_filename_buffer(next_raw, next_name_len, next_clean, sizeof(next_clean));
                                str_to_upper(next_clean);

                                if (strcmp(next_clean, normalized_search) == 0) {
                                    uint32_t ext_lba = dir_block[search_pos + 2] | (dir_block[search_pos + 3] << 8) |
                                                       (dir_block[search_pos + 4] << 16) | (dir_block[search_pos + 5] << 24);
                                    uint32_t ext_size = dir_block[search_pos + 10] | (dir_block[search_pos + 11] << 8) |
                                                        (dir_block[search_pos + 12] << 16) | (dir_block[search_pos + 13] << 24);

                                    entry->extents[entry->extent_count].lba = ext_lba;
                                    entry->extents[entry->extent_count].size = ext_size;
                                    entry->total_size += ext_size;
                                    entry->extent_count++;

                                    if (!(next_flags & 0x80)) break; /* Last extent */
                                }
                            }
                            search_pos += next_len;
                        }
                    }

                    return 1;
                }
            }
            pos += record_len;
        }
    }

    set_error(reader, ISO_ERR_FILE_NOT_FOUND, "File not found in ISO");
    return 0;
}

int iso_reader_extract_file_entry(struct iso_reader *reader, const struct iso_file_entry *entry, const char *output_path) {
    if (!reader || !reader->file || !entry || !output_path) {
        set_error(reader, ISO_ERR_NULL_POINTER, "Invalid parameters for extract_file_entry");
        return 0;
    }

    if (entry->extent_count == 0 || entry->total_size == 0) {
        set_error(reader, ISO_ERR_INVALID_FORMAT, "Invalid file entry (no extents or zero size)");
        return 0;
    }

    FILE *out_file = fopen(output_path, "wb");
    if (!out_file) {
        set_error(reader, ISO_ERR_WRITE_FAILED, "Failed to create output file");
        return 0;
    }

    size_t total_written = 0;

    /* Process each extent in order */
    for (size_t ext = 0; ext < entry->extent_count; ++ext) {
        uint32_t ext_lba = entry->extents[ext].lba;
        uint32_t ext_remaining = entry->extents[ext].size;
        size_t block_idx = 0;

        while (ext_remaining > 0) {
            uint8_t data_block[ISO_BLOCK_SIZE];
            size_t bytes_read = iso_reader_read_block(reader, ext_lba + block_idx, data_block, ISO_BLOCK_SIZE);

            if (bytes_read == 0) {
                set_error(reader, ISO_ERR_READ_FAILED, "Failed to read data block");
                fclose(out_file);
                return 0;
            }

            size_t to_write = (ext_remaining < ISO_BLOCK_SIZE) ? ext_remaining : ISO_BLOCK_SIZE;
            size_t written = fwrite(data_block, 1, to_write, out_file);

            if (written != to_write) {
                set_error(reader, ISO_ERR_WRITE_FAILED, "Failed to write to output file");
                fclose(out_file);
                return 0;
            }

            ext_remaining -= (uint32_t)to_write;
            total_written += to_write;
            ++block_idx;
        }
    }

    fclose(out_file);
    return (total_written == entry->total_size) ? 1 : 0;
}

/* ==================== Validation & Integrity ==================== */

uint32_t iso_reader_calculate_crc32(const uint8_t *data, size_t size) {
    init_crc32_table();
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

int iso_reader_validate_file(struct iso_reader *reader, const char *iso_filename, uint32_t *out_crc) {
    if (!reader || !reader->file || !iso_filename || !out_crc) {
        set_error(reader, ISO_ERR_NULL_POINTER, "Invalid parameters for validate_file");
        return 0;
    }

    struct iso_file_entry entry;
    if (!iso_reader_find_file_entry(reader, iso_filename, &entry)) {
        return 0; /* Error already set */
    }

    if (entry.is_directory) {
        set_error(reader, ISO_ERR_INVALID_FORMAT, "Cannot validate a directory");
        return 0;
    }

    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;

    for (size_t ext = 0; ext < entry.extent_count; ++ext) {
        uint32_t ext_lba = entry.extents[ext].lba;
        uint32_t ext_remaining = entry.extents[ext].size;
        size_t block_idx = 0;

        while (ext_remaining > 0) {
            uint8_t data_block[ISO_BLOCK_SIZE];
            size_t bytes_read = iso_reader_read_block(reader, ext_lba + block_idx, data_block, ISO_BLOCK_SIZE);

            if (bytes_read == 0) {
                set_error(reader, ISO_ERR_READ_FAILED, "Failed to read block during validation");
                return 0;
            }

            size_t to_process = (ext_remaining < ISO_BLOCK_SIZE) ? ext_remaining : ISO_BLOCK_SIZE;
            
            for (size_t i = 0; i < to_process; i++) {
                crc = crc32_table[(crc ^ data_block[i]) & 0xFF] ^ (crc >> 8);
            }

            ext_remaining -= (uint32_t)to_process;
            ++block_idx;
        }
    }

    *out_crc = crc ^ 0xFFFFFFFF;
    return 1;
}

int iso_reader_verify_file_crc(struct iso_reader *reader, const char *iso_filename, uint32_t expected_crc) {
    uint32_t calculated_crc;
    if (!iso_reader_validate_file(reader, iso_filename, &calculated_crc)) {
        return 0;
    }

    if (calculated_crc != expected_crc) {
        set_error(reader, ISO_ERR_CHECKSUM_MISMATCH, "CRC32 checksum mismatch");
        return 0;
    }

    return 1;
}

/* ==================== Error Handling ==================== */

int iso_reader_get_last_error(const struct iso_reader *reader) {
    return reader ? reader->last_error : ISO_ERR_NULL_POINTER;
}

const char* iso_reader_get_last_error_msg(const struct iso_reader *reader) {
    if (!reader) return "Null reader pointer";
    return reader->error_msg[0] ? reader->error_msg : "No error";
}

void iso_reader_clear_error(struct iso_reader *reader) {
    if (reader) {
        reader->last_error = ISO_OK;
        reader->error_msg[0] = '\0';
    }
}
