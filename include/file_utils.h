#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <string>

// Funci�n para leer un archivo binario en un vector de bytes
std::vector<uint8_t> readBinaryFile(const std::string& filePath);

// Remover ";1" dos nomes (versões do padrão ISO)
// Remove ";1" from names (ISO standard versions)
std::string cleanFileName(const std::string& name);

// Funci�n para escribir un archivo binario
void writeBinaryFile(const std::string& filePath, const std::vector<uint8_t>& data);
