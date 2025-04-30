#include "../include/iso_reader.h"
#include <iostream>

int main() {
    std::string isoPath, fileName, outPath;
    std::cout << "Caminho da ISO: ";
    std::getline(std::cin, isoPath);

    std::cout << "Nome do arquivo na ISO (ex: SLUS_212.69): ";
    std::getline(std::cin, fileName);

    std::cout << "Caminho de saída (ex: ./output.elf): ";
    std::getline(std::cin, outPath);

    ISOReader reader(isoPath);
    if (!reader.isOpen()) {
        std::cerr << "Erro ao abrir ISO!" << std::endl;
        return 1;
    }

    if (!reader.extractFileByName(fileName, outPath)) {
        std::cerr << "Falha na extração!" << std::endl;
        return 1;
    }

    std::cout << "Extração concluída com sucesso." << std::endl;
    return 0;
}
