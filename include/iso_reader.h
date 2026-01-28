#ifndef ISO_READER_H
#define ISO_READER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define ISO_BLOCK_SIZE 2048

struct ISOReader {
    FILE *file;
};

// Inicializa o leitor de ISO. Retorna 1 se sucesso, 0 se erro.
int ISOReader_init(struct ISOReader *reader, const char *path);

// Fecha o arquivo ISO
void ISOReader_close(struct ISOReader *reader);

// Retorna 1 se o arquivo está aberto, 0 caso contrário
int ISOReader_isOpen(const struct ISOReader *reader);

// Lê um bloco da ISO (default: 2048 bytes). Retorna número de bytes lidos.
size_t ISOReader_readBlock(struct ISOReader *reader, size_t blockIndex, uint8_t *buffer, size_t blockSize);

// Lê um intervalo qualquer de bytes (offset + tamanho). Retorna número de bytes lidos.
size_t ISOReader_readRange(struct ISOReader *reader, size_t offset, size_t length, uint8_t *buffer);

// Busca e retorna o conteúdo do SYSTEM.CNF se encontrado. Retorna 1 se achou, 0 se não. O conteúdo vai para o buffer.
int ISOReader_findSystemCNF(struct ISOReader *reader, char *buffer, size_t bufsize);

// Lê e lista os arquivos do diretório raiz da ISO. Recebe ponteiro para função callback para cada nome encontrado.
void ISOReader_readDirectory(struct ISOReader *reader, void (*onFile)(const char *filename, void *userdata), void *userdata);

// Extrai um arquivo da ISO pelo nome e salva no caminho de saída. Retorna 1 se sucesso, 0 se erro.
int ISOReader_extractFileByName(struct ISOReader *reader, const char *isoFileName, const char *outputPath);

#endif // ISO_READER_H