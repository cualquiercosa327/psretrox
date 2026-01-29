#ifndef ISO_READER_H
#define ISO_READER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define ISO_BLOCK_SIZE 2048

struct ISOReader {
    FILE *file;
};

/**
 * @brief Initializes the ISO reader. Returns 1 on success, 0 on failure.
 * @param reader Pointer to ISOReader struct
 * @param path Path to the ISO file
 */
int ISOReader_init(struct ISOReader *reader, const char *path);

/**
 * @brief Closes the ISO reader and releases resources.
 * @param reader Pointer to ISOReader struct
 */
void ISOReader_close(struct ISOReader *reader);

/**
 * @brief Checks if the ISO file is open. Returns 1 if open, 0 if not.
 * @param reader Pointer to ISOReader struct
 */
int ISOReader_isOpen(const struct ISOReader *reader);

/**
 * @brief Reads a block of data from the ISO. Returns number of bytes read.
 * @param reader Pointer to ISOReader struct
 * @param blockIndex Index of the block to read
 * @param buffer Buffer to store the read data
 * @param blockSize Size of each block (usually 2048 bytes)
 */
size_t ISOReader_readBlock(struct ISOReader *reader, size_t blockIndex, uint8_t *buffer, size_t blockSize);

/**
 * @brief Reads a range of bytes from the ISO. Returns number of bytes read.
 * @param reader Pointer to ISOReader struct
 * @param offset Offset in bytes from the start of the ISO
 * @param length Number of bytes to read
 * @param buffer Buffer to store the read data
 */
size_t ISOReader_readRange(struct ISOReader *reader, size_t offset, size_t length, uint8_t *buffer);

/**
 * @brief Searches and returns the content of SYSTEM.CNF if found. Returns 1 if found, 0 if not. The content is placed in the buffer.
 * @param reader Pointer to ISOReader struct
 * @param buffer Buffer to store the content
 * @param bufsize Size of the buffer
 */
int ISOReader_findSystemCNF(struct ISOReader *reader, char *buffer, size_t bufsize);

/**
 * @brief Reads and lists files in the root directory of the ISO. Receives a callback function pointer for each found filename.
 * @param reader Pointer to ISOReader struct
 * @param onFile Callback function called for each filename
 * @param userdata User data passed to the callback
 */
void ISOReader_readDirectory(struct ISOReader *reader, void (*onFile)(const char *filename, void *userdata), void *userdata);

/**
 * @brief Extracts a file from the ISO by name and saves it to the output path. Returns 1 on success, 0 on failure.
 * @param reader Pointer to ISOReader struct
 * @param isoFileName Name of the file inside the ISO
 * @param outputPath Path to save the extracted file
 */
int ISOReader_extractFileByName(struct ISOReader *reader, const char *isoFileName, const char *outputPath);

#endif // ISO_READER_H