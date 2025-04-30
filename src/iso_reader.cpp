#include "../include/iso_reader.h"
#include "../include/file_utils.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

ISOReader::ISOReader(const std::string &path)
{
    file.open(path, std::ios::binary);
}

ISOReader::~ISOReader()
{
    if (file.is_open())
    {
        file.close();
    }
}

bool ISOReader::isOpen() const
{
    return file.is_open();
}

std::vector<uint8_t> ISOReader::readBlock(size_t blockIndex, size_t blockSize)
{
    std::vector<uint8_t> buffer(blockSize);

    if (!file.is_open())
        return {};

    file.seekg(blockIndex * blockSize, std::ios::beg);
    file.read(reinterpret_cast<char *>(buffer.data()), blockSize);

    return buffer;
}

std::vector<uint8_t> ISOReader::readRange(size_t offset, size_t length)
{
    std::vector<uint8_t> buffer(length);

    if (!file.is_open())
        return {};

    file.seekg(offset, std::ios::beg);
    file.read(reinterpret_cast<char *>(buffer.data()), length);

    return buffer;
}

std::string ISOReader::findSystemCNF()
{
    if (!file.is_open())
        return "Arquivo não aberto / File not open";

    // Verifica os primeiros 300 blocos da ISO por SYSTEM.CNF
    // Check first 300 blocks for SYSTEM.CNF
    const size_t maxBlocks = 300;
    const size_t blockSize = 2048;

    for (size_t i = 0; i < maxBlocks; ++i)
    {
        auto block = readBlock(i, blockSize);

        auto it = std::search(block.begin(), block.end(),
                              "SYSTEM.CNF", "SYSTEM.CNF" + 10);
        if (it != block.end())
        {
            size_t offset = std::distance(block.begin(), it);

            // Tenta ler cerca de 512 bytes do ponto encontrado
            std::string result;
            for (size_t j = offset; j < offset + 512 && j < block.size(); ++j)
            {
                if (isprint(block[j]) || block[j] == '\n')
                {
                    result += static_cast<char>(block[j]);
                }
            }
            return result;
        }
    }

    return "SYSTEM.CNF não encontrado / not found";
}

// Implementação da função readDirectory
// Implementation of the readDirectory function
std::vector<std::string> ISOReader::readDirectory()
{
    std::vector<std::string> fileNames;

    auto block = readBlock(16);
    if (block.empty())
    {
        std::cerr << "Erro ao ler o Volume Descriptor." << std::endl;
        return fileNames;
    }

    // Root Directory Record começa em byte 156 e tem 34 bytes (ISO 9660)
    size_t rootDirRecordOffset = 156;
    uint32_t rootDirLBA = *reinterpret_cast<uint32_t *>(&block[rootDirRecordOffset + 2]);
    uint32_t rootDirSize = *reinterpret_cast<uint32_t *>(&block[rootDirRecordOffset + 10]);

    size_t numBlocks = (rootDirSize + 2047) / 2048;

    for (size_t i = 0; i < numBlocks; ++i)
    {
        auto dirBlock = readBlock(rootDirLBA + i);
        if (dirBlock.empty())
            continue;

        size_t pos = 0;
        while (pos < dirBlock.size())
        {
            uint8_t recordLength = dirBlock[pos];
            if (recordLength == 0)
                break;
            uint8_t nameLength = dirBlock[pos + 32];
            std::string name(reinterpret_cast<char *>(&dirBlock[pos + 33]), nameLength);

            // Ignora entradas especiais '.' e '..'
            // Ignore special entries '.' and '..'
            if (name != "\0" && name != "\1")
            {
                fileNames.push_back(cleanFileName(name));
            }

            pos += recordLength;
        }
    }

    return fileNames;
}

bool ISOReader::extractFileByName(const std::string &isoFileName, const std::string &outputPath)
{
    std::cout << "Iniciando a busca e extração do arquivo: " << isoFileName << std::endl;

    auto normalizeName = [](const std::string &name)
    {
        std::string normalized = name;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
        return normalized;
    };

    std::string normalizedSearchName = normalizeName(isoFileName);

    auto block = readBlock(16); // Bloco 16 contém o Primary Volume Descriptor
    if (block.empty())
    {
        std::cerr << "Erro ao ler o Primary Volume Descriptor." << std::endl;
        return false;
    }

    // Offset para o Root Directory Record (byte 156 no PVD)
    size_t rootDirRecordOffset = 156;
    uint32_t rootDirLBA = *reinterpret_cast<uint32_t *>(&block[rootDirRecordOffset + 2]);
    uint32_t rootDirSize = *reinterpret_cast<uint32_t *>(&block[rootDirRecordOffset + 10]);

    size_t numRootBlocks = (rootDirSize + 2047) / 2048;

    for (size_t i = 0; i < numRootBlocks; ++i)
    {
        auto dirBlock = readBlock(rootDirLBA + i);
        if (dirBlock.empty())
            continue;

        size_t pos = 0;
        while (pos < dirBlock.size())
        {
            uint8_t recordLength = dirBlock[pos];
            if (recordLength == 0)
                break;

            uint8_t nameLength = dirBlock[pos + 32];
            std::string name(reinterpret_cast<char *>(&dirBlock[pos + 33]), nameLength);

            if (nameLength > 0)
            {
                if (normalizeName(cleanFileName(name)) == normalizedSearchName)
                {
                    // Encontrou o arquivo!
                    uint32_t fileLBA = *reinterpret_cast<uint32_t *>(&dirBlock[pos + 2]);
                    uint32_t fileSize = *reinterpret_cast<uint32_t *>(&dirBlock[pos + 10]);

                    std::cout << "Arquivo encontrado: " << name << ", LBA do dado: " << fileLBA << ", Tamanho: " << fileSize << " bytes." << std::endl;

                    size_t numFileBlocksToRead = (fileSize + 2047) / 2048;
                    std::vector<uint8_t> fileData;

                    for (size_t j = 0; j < numFileBlocksToRead; ++j)
                    {
                        auto dataBlock = readBlock(fileLBA + j);
                        if (dataBlock.empty())
                        {
                            std::cerr << "Erro ao ler bloco " << j << " do arquivo (LBA: " << fileLBA + j << ")." << std::endl;
                            return false;
                        }
                        fileData.insert(fileData.end(), dataBlock.begin(), dataBlock.end());
                    }

                    std::cout << "Dados do arquivo lidos. Total de bytes lidos: " << fileData.size() << std::endl;

                    // Gravar no arquivo de saída
                    std::ofstream outFile(outputPath, std::ios::binary);
                    if (!outFile)
                    {
                        std::cerr << "Erro ao abrir arquivo de saída: " << outputPath << std::endl;
                        return false;
                    }

                    // Escrever apenas a quantidade correta de bytes do arquivo
                    outFile.write(reinterpret_cast<const char *>(fileData.data()), fileSize);
                    std::cout << "Arquivo extraído com sucesso para: " << outputPath << std::endl;
                    return true;
                }
            }
            pos += recordLength;
        }
    }

    std::cerr << "Arquivo não encontrado na ISO: " << isoFileName << std::endl;
    return false;
}