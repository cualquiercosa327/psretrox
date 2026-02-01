#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

uint8_t* read_binary_file(const char* filePath, size_t* outSize) {
    FILE* file = fopen(filePath, "rb");
    if (!file) {
        perror("Failed to open file for reading");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        perror("Failed to allocate memory for file data");
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != fileSize) {
        perror("Failed to read complete file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *outSize = fileSize;
    return buffer;
}

char* clean_iso_version_suffix(const char* name) {
    const char* semicolonPos = strstr(name, ";1");
    size_t cleanLength = semicolonPos ? (size_t)(semicolonPos - name) : strlen(name);

    char* cleanName = (char*)malloc(cleanLength + 1);
    if (!cleanName) {
        perror("Failed to allocate memory for cleaned file name");
        return NULL;
    }

    strncpy(cleanName, name, cleanLength);
    cleanName[cleanLength] = '\0';

    return cleanName;
}

void clean_iso_filename_buffer(const char *src, size_t len, char *dst, size_t dstsize) {
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

void write_binary_file(const char* filePath, const uint8_t* data, size_t dataSize) {
    FILE* file = fopen(filePath, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        return;
    }

    size_t bytesWritten = fwrite(data, 1, dataSize, file);
    if (bytesWritten != dataSize) {
        perror("Failed to write complete data to file");
    }

    fclose(file);
}

void str_to_upper(char *s) {
    while (*s) {
        *s = (char)toupper((unsigned char)*s);
        ++s;
    }
}