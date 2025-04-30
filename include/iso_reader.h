#ifndef ISO_READER_H
#define ISO_READER_H

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>

class ISOReader
{
public:
    ISOReader(const std::string &path);
    ~ISOReader();

    bool isOpen() const;

    // Lê um bloco da ISO (por padrão, 2048 bytes)
    // Reads a block from the ISO (default: 2048 bytes)
    std::vector<uint8_t> readBlock(size_t blockIndex, size_t blockSize = 2048);

    // Lê um intervalo qualquer de bytes (offset + tamanho)
    // Reads a range of bytes (offset + length)
    std::vector<uint8_t> readRange(size_t offset, size_t length);

    // Busca e retorna o conteúdo do SYSTEM.CNF se encontrado
    // Searches and returns the content of SYSTEM.CNF if found
    std::string findSystemCNF();

    // Lê e lista os arquivos do diretório raiz da ISO
    // Reads and lists the files in the root directory of the ISO
    std::vector<std::string> readDirectory();

private:
    std::ifstream file;
};

#endif // ISO_READER_H
