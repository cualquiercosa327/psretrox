#include "../include/iso_reader.h"
#include <iostream>

int main() {
    std::string path;
    std::cout << "Digite o caminho para a ISO: ";
    std::getline(std::cin, path);

    ISOReader reader(path);

    if (!reader.isOpen()) {
        std::cerr << "Erro ao abrir ISO.\n";
        return 1;
    }

    std::cout << "Arquivo aberto com sucesso!\n";

    // Teste leitura do bloco 16 (descritor de volume)
    auto block = reader.readBlock(16);
    std::cout << "Conteúdo do bloco 16:\n";
    for (char c : block) {
        if (isprint(c)) std::cout << c;
    }
    std::cout << "\n\n";

    // Teste leitura do SYSTEM.CNF
    std::string systemCnf = reader.findSystemCNF();
    std::cout << "Conteúdo do SYSTEM.CNF:\n" << systemCnf << "\n";

    return 0;
}
