#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdint.h>

/**
 * @brief Reads a binary file into a dynamically allocated byte buffer.
 * @param file_path Path to the binary file
 * @param out_size Pointer to store the size of the read data
 * @return Pointer to buffer containing the file data (must be freed by the caller)
 */
uint8_t* read_binary_file(const char* file_path, size_t* out_size);

/**
 * @brief Removes the version suffix (";1") from an ISO 9660 file name.
 * @param name Original file name (null-terminated string)
 * @return Cleaned file name (dynamically allocated, must be freed)
 */
char* clean_iso_version_suffix(const char* name);

/**
 * @brief Cleans an ISO 9660 file name buffer, removing version suffix (";1") and trailing spaces.
 * @param src Source filename buffer (may not be null-terminated)
 * @param len Length of source filename
 * @param dst Destination buffer
 * @param dstsize Size of destination buffer
 */
void clean_iso_filename_buffer(const char *src, size_t len, char *dst, size_t dstsize);

/**
 * @brief Writes a binary buffer to a file.
 * @param file_path Path to the binary file to create
 * @param data Pointer to the byte data to write
 * @param data_size Size of the data to write
 */
void write_binary_file(const char* file_path, const uint8_t* data, size_t data_size);

/**
 * @brief Converts a string to uppercase in place.
 * @param s The string to convert (must be null-terminated)
 */
void str_to_upper(char *s);

#endif // FILE_UTILS_H