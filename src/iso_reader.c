#include "../include/iso_reader.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/**
* @brief Aux function to clean ISO 9660 filenames (remove version and trailing spaces)
* @param src Source filename
* @param len Length of source filename
* @param dst Destination buffer
* @param dstsize Size of destination buffer
*/
static void clean_filename(const char *src, size_t len, char *dst, size_t dstsize) {
    size_t i, j = 0;
    for (i = 0; i < len && j < dstsize - 1; ++i) {
        if (src[i] == ';')
            break;
        if (src[i] == '\0')
            break;
        dst[j++] = src[i];
    }
    // Remove trailing spaces
    while (j > 0 && dst[j - 1] == ' ')
        --j;

    dst[j] = '\0';
}

/**
 * @brief Converts a string to uppercase in place
 * @param s The string to convert
 */
static void str_to_upper(char *s) {
    while (*s) {
        *s = (char)toupper((unsigned char)*s);
        ++s;
    }
}

int ISOReader_init(struct ISOReader *reader, const char *path) {
    if (!reader || !path)
        return 0;

    reader->file = fopen(path, "rb");
    return reader->file != NULL;
}

void ISOReader_close(struct ISOReader *reader) {
    if (reader && reader->file) {
        fclose(reader->file);
        reader->file = NULL;
    }
}

int ISOReader_isOpen(const struct ISOReader *reader) {
    return reader && reader->file != NULL;
}

size_t ISOReader_readBlock(struct ISOReader *reader, size_t blockIndex, uint8_t *buffer, size_t blockSize) {
    if (!reader || !reader->file || !buffer)
        return 0;

    long offset = (long)(blockIndex * blockSize);
    if (fseek(reader->file, offset, SEEK_SET) != 0)
        return 0;

    return fread(buffer, 1, blockSize, reader->file);
}

size_t ISOReader_readRange(struct ISOReader *reader, size_t offset, size_t length, uint8_t *buffer) {
    if (!reader || !reader->file || !buffer)
        return 0;

    if (fseek(reader->file, (long)offset, SEEK_SET) != 0)
        return 0;

    return fread(buffer, 1, length, reader->file);
}

int ISOReader_findSystemCNF(struct ISOReader *reader, char *buffer, size_t bufsize) {
    if (!reader || !reader->file || !buffer || bufsize == 0)
        return 0;

    const size_t maxBlocks = 300;
    uint8_t block[ISO_BLOCK_SIZE];
    const char *needle = "SYSTEM.CNF";
    size_t needleLen = 10;

    for (size_t i = 0; i < maxBlocks; ++i) {
        size_t bytesRead = ISOReader_readBlock(reader, i, block, ISO_BLOCK_SIZE);
        if (bytesRead == 0)
            continue;

        // Busca "SYSTEM.CNF" no bloco
        for (size_t j = 0; j + needleLen <= bytesRead; ++j) {
            if (memcmp(&block[j], needle, needleLen) == 0) {
                // Encontrou! Copia até 512 bytes printáveis
                size_t k = 0;
                for (size_t m = j; m < j + 512 && m < bytesRead && k < bufsize - 1; ++m) {
                    if (isprint(block[m]) || block[m] == '\n')
                        buffer[k++] = (char)block[m];
                }
                buffer[k] = '\0';
                return 1;
            }
        }
    }

    buffer[0] = '\0';
    return 0;
}

void ISOReader_readDirectory(struct ISOReader *reader, void (*onFile)(const char *filename, void *userdata), void *userdata) {
    if (!reader || !reader->file || !onFile)
        return;

    uint8_t block[ISO_BLOCK_SIZE];

    // Lê o Primary Volume Descriptor (bloco 16)
    if (ISOReader_readBlock(reader, 16, block, ISO_BLOCK_SIZE) == 0)
        return;

    // Root Directory Record começa no byte 156 do PVD (ISO 9660)
    size_t rootDirRecordOffset = 156;

    // LBA do diretório raiz (little-endian, 4 bytes no offset +2)
    uint32_t rootDirLBA = block[rootDirRecordOffset + 2]
                        | (block[rootDirRecordOffset + 3] << 8)
                        | (block[rootDirRecordOffset + 4] << 16)
                        | (block[rootDirRecordOffset + 5] << 24);

    // Tamanho do diretório raiz (little-endian, 4 bytes no offset +10)
    uint32_t rootDirSize = block[rootDirRecordOffset + 10]
                         | (block[rootDirRecordOffset + 11] << 8)
                         | (block[rootDirRecordOffset + 12] << 16)
                         | (block[rootDirRecordOffset + 13] << 24);

    size_t numBlocks = (rootDirSize + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

    for (size_t i = 0; i < numBlocks; ++i) {
        uint8_t dirBlock[ISO_BLOCK_SIZE];
        if (ISOReader_readBlock(reader, rootDirLBA + i, dirBlock, ISO_BLOCK_SIZE) == 0)
            continue;

        size_t pos = 0;
        while (pos < ISO_BLOCK_SIZE) {
            uint8_t recordLength = dirBlock[pos];
            if (recordLength == 0)
                break;

            uint8_t nameLength = dirBlock[pos + 32];
            if (nameLength > 0 && pos + 33 + nameLength <= ISO_BLOCK_SIZE) {
                char rawName[256];
                char cleanName[256];

                if (nameLength > 255)
                    nameLength = 255;

                memcpy(rawName, &dirBlock[pos + 33], nameLength);
                rawName[nameLength] = '\0';

                // Ignora entradas especiais '.' e '..'
                if (!(nameLength == 1 && (rawName[0] == '\0' || rawName[0] == '\1'))) {
                    clean_filename(rawName, nameLength, cleanName, sizeof(cleanName));
                    onFile(cleanName, userdata);
                }
            }

            pos += recordLength;
        }
    }
}


int ISOReader_extractFileByName(struct ISOReader *reader, const char *isoFileName, const char *outputPath)
{
    if (!reader || !reader->file || !isoFileName || !outputPath)
        return 0;

    uint8_t block[ISO_BLOCK_SIZE];

    // Normaliza o nome buscado para maiúsculas
    char normalizedSearch[256];
    strncpy(normalizedSearch, isoFileName, sizeof(normalizedSearch) - 1);
    normalizedSearch[sizeof(normalizedSearch) - 1] = '\0';
    str_to_upper(normalizedSearch);

    // Lê o Primary Volume Descriptor (bloco 16)
    if (ISOReader_readBlock(reader, 16, block, ISO_BLOCK_SIZE) == 0)
        return 0;

    size_t rootDirRecordOffset = 156;

    uint32_t rootDirLBA = block[rootDirRecordOffset + 2]
                        | (block[rootDirRecordOffset + 3] << 8)
                        | (block[rootDirRecordOffset + 4] << 16)
                        | (block[rootDirRecordOffset + 5] << 24);

    uint32_t rootDirSize = block[rootDirRecordOffset + 10]
                         | (block[rootDirRecordOffset + 11] << 8)
                         | (block[rootDirRecordOffset + 12] << 16)
                         | (block[rootDirRecordOffset + 13] << 24);

    size_t numBlocks = (rootDirSize + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

    for (size_t i = 0; i < numBlocks; ++i) {
        uint8_t dirBlock[ISO_BLOCK_SIZE];
        if (ISOReader_readBlock(reader, rootDirLBA + i, dirBlock, ISO_BLOCK_SIZE) == 0)
            continue;

        size_t pos = 0;
        while (pos < ISO_BLOCK_SIZE) {
            uint8_t recordLength = dirBlock[pos];
            if (recordLength == 0)
                break;

            uint8_t nameLength = dirBlock[pos + 32];
            if (nameLength > 0 && pos + 33 + nameLength <= ISO_BLOCK_SIZE) {
                char rawName[256];
                char cleanName[256];

                if (nameLength > 255)
                    nameLength = 255;

                memcpy(rawName, &dirBlock[pos + 33], nameLength);
                rawName[nameLength] = '\0';

                clean_filename(rawName, nameLength, cleanName, sizeof(cleanName));
                str_to_upper(cleanName);

                if (strcmp(cleanName, normalizedSearch) == 0)
                {
                    // Encontrou o arquivo!
                    uint32_t fileLBA = dirBlock[pos + 2]
                                     | (dirBlock[pos + 3] << 8)
                                     | (dirBlock[pos + 4] << 16)
                                     | (dirBlock[pos + 5] << 24);

                    uint32_t fileSize = dirBlock[pos + 10]
                                      | (dirBlock[pos + 11] << 8)
                                      | (dirBlock[pos + 12] << 16)
                                      | (dirBlock[pos + 13] << 24);

                    // Abre arquivo de saída
                    FILE *outFile = fopen(outputPath, "wb");
                    if (!outFile)
                        return 0;

                    size_t bytesRemaining = fileSize;
                    size_t blockIdx = 0;

                    while (bytesRemaining > 0) {
                        uint8_t dataBlock[ISO_BLOCK_SIZE];
                        size_t bytesRead = ISOReader_readBlock(reader, fileLBA + blockIdx, dataBlock, ISO_BLOCK_SIZE);

                        if (bytesRead == 0) {
                            fclose(outFile);
                            return 0;
                        }

                        size_t toWrite = (bytesRemaining < ISO_BLOCK_SIZE) ? bytesRemaining : ISO_BLOCK_SIZE;
                        fwrite(dataBlock, 1, toWrite, outFile);

                        bytesRemaining -= toWrite;
                        ++blockIdx;
                    }

                    fclose(outFile);
                    return 1;
                }
            }

            pos += recordLength;
        }
    }

    return 0;
}
