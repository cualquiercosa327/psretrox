#include "../include/iso_reader.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring> // para memcmp

ISOReader::ISOReader(const std::string& path) {
    file.open(path, std::ios::binary);
}

ISOReader::~ISOReader() {
    if (file.is_open()) {
        file.close();
    }
}

bool ISOReader::isOpen() const {
    return file.is_open();
}

std::vector<uint8_t> ISOReader::readBlock(size_t blockIndex, size_t blockSize) {
    std::vector<uint8_t> buffer(blockSize);

    if (!file.is_open()) return {};

    file.seekg(blockIndex * blockSize, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), blockSize);

    return buffer;
}

std::vector<uint8_t> ISOReader::readRange(size_t offset, size_t length) {
    std::vector<uint8_t> buffer(length);

    if (!file.is_open()) return {};

    file.seekg(offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), length);

    return buffer;
}

std::string ISOReader::findSystemCNF() {
    if (!file.is_open()) return "Arquivo não aberto / File not open";

    // Verifica os primeiros 300 blocos da ISO por SYSTEM.CNF
    // Check first 300 blocks for SYSTEM.CNF
    const size_t maxBlocks = 300;
    const size_t blockSize = 2048;

    for (size_t i = 0; i < maxBlocks; ++i) {
        auto block = readBlock(i, blockSize);

        auto it = std::search(block.begin(), block.end(),
                              "SYSTEM.CNF", "SYSTEM.CNF" + 10);
        if (it != block.end()) {
            size_t offset = std::distance(block.begin(), it);

            // Tenta ler cerca de 512 bytes do ponto encontrado
            std::string result;
            for (size_t j = offset; j < offset + 512 && j < block.size(); ++j) {
                if (isprint(block[j]) || block[j] == '\n') {
                    result += static_cast<char>(block[j]);
                }
            }
            return result;
        }
    }

    return "SYSTEM.CNF não encontrado / not found";
}
