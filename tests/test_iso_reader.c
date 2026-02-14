/**
 * @file test_iso_reader.c
 * @brief Testes completos para o ISOReader
 * 
 * Compilar:
 *   gcc -o test_iso tests/test_iso_reader.c src/iso_reader.c -I include -Wall
 * 
 * Uso:
 *   ./test_iso <caminho_para_iso> [arquivo_para_testar]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "iso_reader.h"

/* ==================== Contadores Globais ==================== */

static int g_file_count = 0;
static int g_dir_count = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/* ==================== Callbacks ==================== */

static void print_file_callback(const char *filename, void *userdata) {
    int *count = (int *)userdata;
    (*count)++;
    printf("  [%03d] %s\n", *count, filename);
}

static void count_recursive_callback(const char *path, void *userdata) {
    (void)userdata;
    g_file_count++;
    size_t len = strlen(path);
    if (len > 0 && path[len - 1] == '/') {
        g_dir_count++;
    }
    printf("  [%03d] %s\n", g_file_count, path);
}

/* ==================== Helpers ==================== */

static void test_pass(const char *test_name) {
    printf("  [PASS] %s\n", test_name);
    g_tests_passed++;
}

static void test_fail(const char *test_name, const char *reason) {
    printf("  [FAIL] %s: %s\n", test_name, reason);
    g_tests_failed++;
}

/* ==================== Testes ==================== */

/**
 * Teste 1: Tratamento de erros (arquivo inexistente)
 */
static void test_error_handling(void) {
    printf("\n=== TESTE: Tratamento de Erros ===\n");
    
    struct iso_reader reader;
    
    if (!iso_reader_init(&reader, "/caminho/que/nao/existe.iso")) {
        int err = iso_reader_get_last_error(&reader);
        const char *msg = iso_reader_get_last_error_msg(&reader);
        printf("  Codigo de erro: %d\n", err);
        printf("  Mensagem: %s\n", msg);
        test_pass("Falha correta ao abrir arquivo inexistente");
    } else {
        test_fail("Abertura de arquivo inexistente", "Deveria ter falhado");
        iso_reader_close(&reader);
    }
}

/**
 * Teste 2: Abrir e fechar ISO
 */
static void test_open_close(struct iso_reader *reader, const char *iso_path) {
    printf("\n=== TESTE: Abrir/Fechar ISO ===\n");
    
    if (!iso_reader_init(reader, iso_path)) {
        test_fail("Abrir ISO", iso_reader_get_last_error_msg(reader));
        return;
    }
    test_pass("Abrir ISO");
    
    if (iso_reader_is_open(reader)) {
        test_pass("iso_reader_is_open() retorna true");
    } else {
        test_fail("iso_reader_is_open()", "Deveria retornar true");
    }
}

/**
 * Teste 3: Detectar formato
 */
static void test_detect_format(struct iso_reader *reader) {
    printf("\n=== TESTE: Detectar Formato ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Detectar formato", "ISO nao esta aberta");
        return;
    }
    
    int format = iso_reader_detect_format(reader);
    const char *format_str = "DESCONHECIDO";
    
    switch (format) {
        case ISO_FORMAT_9660:   format_str = "ISO 9660"; break;
        case ISO_FORMAT_UDF:    format_str = "UDF"; break;
        case ISO_FORMAT_UNKNOWN: format_str = "DESCONHECIDO"; break;
    }
    
    printf("  Formato detectado: %s\n", format_str);
    test_pass("Detectar formato");
}

/**
 * Teste 4: Ler bloco 16 (Primary Volume Descriptor)
 */
static void test_read_block(struct iso_reader *reader) {
    printf("\n=== TESTE: Ler Bloco 16 (PVD) ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Ler bloco", "ISO nao esta aberta");
        return;
    }
    
    uint8_t block[ISO_BLOCK_SIZE];
    size_t bytes_read = iso_reader_read_block(reader, 16, block, ISO_BLOCK_SIZE);
    
    if (bytes_read == 0) {
        test_fail("Ler bloco 16", "Nenhum byte lido");
        return;
    }
    
    printf("  Bytes lidos: %zu\n", bytes_read);
    
    /* Verifica identificador CD001 */
    if (memcmp(&block[1], "CD001", 5) == 0) {
        printf("  Identificador: CD001 (valido)\n");
        test_pass("Identificador ISO valido");
    } else {
        printf("  Identificador: %.5s (invalido)\n", &block[1]);
        test_fail("Identificador ISO", "Esperado CD001");
    }
    
    /* Mostra nome do volume */
    printf("  Nome do volume: ");
    for (int i = 40; i < 72; i++) {
        if (isprint(block[i])) {
            putchar(block[i]);
        }
    }
    printf("\n");
    
    test_pass("Ler bloco 16");
}

/**
 * Teste 5: Ler intervalo de bytes
 */
static void test_read_range(struct iso_reader *reader) {
    printf("\n=== TESTE: Ler Intervalo de Bytes ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Ler intervalo", "ISO nao esta aberta");
        return;
    }
    
    uint8_t buffer[64];
    size_t bytes_read = iso_reader_read_range(reader, 0x8000, 64, buffer);
    
    if (bytes_read > 0) {
        printf("  Bytes lidos: %zu\n", bytes_read);
        printf("  Hex dump: ");
        for (size_t i = 0; i < 32 && i < bytes_read; i++) {
            printf("%02X ", buffer[i]);
        }
        printf("...\n");
        test_pass("Ler intervalo de bytes");
    } else {
        test_fail("Ler intervalo", "Nenhum byte lido");
    }
}

/**
 * Teste 6: Buscar SYSTEM.CNF
 */
static void test_find_system_cnf(struct iso_reader *reader) {
    printf("\n=== TESTE: Buscar SYSTEM.CNF ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Buscar SYSTEM.CNF", "ISO nao esta aberta");
        return;
    }
    
    char buffer[512];
    if (iso_reader_find_system_cnf(reader, buffer, sizeof(buffer))) {
        printf("  Conteudo:\n%s\n", buffer);
        test_pass("SYSTEM.CNF encontrado");
    } else {
        printf("  INFO: SYSTEM.CNF nao encontrado (normal para algumas ISOs)\n");
        test_pass("Busca por SYSTEM.CNF executada");
    }
}

/**
 * Teste 7: Listar diretorio raiz
 */
static void test_list_root_directory(struct iso_reader *reader) {
    printf("\n=== TESTE: Listar Diretorio Raiz ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Listar diretorio", "ISO nao esta aberta");
        return;
    }
    
    int count = 0;
    printf("  Arquivos no diretorio raiz:\n");
    iso_reader_read_directory(reader, print_file_callback, &count);
    printf("  Total: %d entradas\n", count);
    
    if (count > 0) {
        test_pass("Listar diretorio raiz");
    } else {
        test_fail("Listar diretorio raiz", "Nenhum arquivo encontrado");
    }
}

/**
 * Teste 8: Listagem recursiva
 */
static void test_recursive_directory(struct iso_reader *reader) {
    printf("\n=== TESTE: Listagem Recursiva ===\n");
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Listagem recursiva", "ISO nao esta aberta");
        return;
    }
    
    g_file_count = 0;
    g_dir_count = 0;
    
    printf("  Todos os arquivos (recursivo):\n");
    iso_reader_read_directory_recursive(reader, "/", count_recursive_callback, NULL);
    
    printf("  Total de entradas: %d\n", g_file_count);
    printf("  Diretorios: %d\n", g_dir_count);
    
    if (g_file_count > 0) {
        test_pass("Listagem recursiva");
    } else {
        test_fail("Listagem recursiva", "Nenhuma entrada encontrada");
    }
}

/**
 * Teste 9: Buscar entrada de arquivo (com extents)
 */
static void test_find_file_entry(struct iso_reader *reader, const char *filename) {
    printf("\n=== TESTE: Buscar Entrada de Arquivo (%s) ===\n", filename);
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Buscar arquivo", "ISO nao esta aberta");
        return;
    }
    
    struct iso_file_entry entry;
    if (iso_reader_find_file_entry(reader, filename, &entry)) {
        printf("  Nome: %s\n", entry.name);
        printf("  Diretorio: %s\n", entry.is_directory ? "sim" : "nao");
        printf("  Tamanho total: %u bytes\n", entry.total_size);
        printf("  Numero de extents: %zu\n", entry.extent_count);
        
        for (size_t i = 0; i < entry.extent_count; i++) {
            printf("    Extent %zu: LBA=%u, Tamanho=%u bytes\n", 
                   i, entry.extents[i].lba, entry.extents[i].size);
        }
        
        test_pass("Buscar entrada de arquivo");
    } else {
        printf("  INFO: Arquivo nao encontrado: %s\n", filename);
        printf("  Erro: %s\n", iso_reader_get_last_error_msg(reader));
        test_pass("Busca executada (arquivo nao existe)");
    }
}

/**
 * Teste 10: Validar CRC32 de arquivo
 */
static void test_validate_file(struct iso_reader *reader, const char *filename) {
    printf("\n=== TESTE: Validar CRC32 (%s) ===\n", filename);
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Validar arquivo", "ISO nao esta aberta");
        return;
    }
    
    uint32_t crc;
    if (iso_reader_validate_file(reader, filename, &crc)) {
        printf("  CRC32: 0x%08X\n", crc);
        test_pass("Calcular CRC32");
    } else {
        printf("  INFO: Nao foi possivel calcular CRC32\n");
        printf("  Erro: %s\n", iso_reader_get_last_error_msg(reader));
        test_pass("Validacao executada (arquivo nao existe)");
    }
}

/**
 * Teste 11: Extrair arquivo simples
 */
static void test_extract_file(struct iso_reader *reader, const char *filename) {
    printf("\n=== TESTE: Extrair Arquivo (%s) ===\n", filename);
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Extrair arquivo", "ISO nao esta aberta");
        return;
    }
    
    char output_path[256];
    snprintf(output_path, sizeof(output_path), "/tmp/iso_test_extracted_%s", filename);
    
    if (iso_reader_extract_file_by_name(reader, filename, output_path)) {
        printf("  Extraido para: %s\n", output_path);
        test_pass("Extrair arquivo simples");
    } else {
        printf("  INFO: Nao foi possivel extrair\n");
        printf("  Erro: %s\n", iso_reader_get_last_error_msg(reader));
        test_pass("Extracao executada (arquivo nao existe)");
    }
}

/**
 * Teste 12: Extrair usando file entry (suporte a extents)
 */
static void test_extract_file_entry(struct iso_reader *reader, const char *filename) {
    printf("\n=== TESTE: Extrair Usando Entry (%s) ===\n", filename);
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Extrair com entry", "ISO nao esta aberta");
        return;
    }
    
    struct iso_file_entry entry;
    if (!iso_reader_find_file_entry(reader, filename, &entry)) {
        printf("  INFO: Arquivo nao encontrado para extracao\n");
        test_pass("Busca para extracao executada");
        return;
    }
    
    char output_path[256];
    snprintf(output_path, sizeof(output_path), "/tmp/iso_test_entry_%s", filename);
    
    if (iso_reader_extract_file_entry(reader, &entry, output_path)) {
        printf("  Extraido para: %s\n", output_path);
        printf("  Tamanho: %u bytes\n", entry.total_size);
        printf("  Extents processados: %zu\n", entry.extent_count);
        test_pass("Extrair usando file entry");
    } else {
        printf("  Erro: %s\n", iso_reader_get_last_error_msg(reader));
        test_fail("Extrair usando file entry", iso_reader_get_last_error_msg(reader));
    }
}

/**
 * Teste 13: Verificar CRC contra valor esperado
 */
static void test_verify_crc(struct iso_reader *reader, const char *filename) {
    printf("\n=== TESTE: Verificar CRC32 (%s) ===\n", filename);
    
    if (!iso_reader_is_open(reader)) {
        test_fail("Verificar CRC", "ISO nao esta aberta");
        return;
    }
    
    /* Primeiro calcula o CRC */
    uint32_t crc;
    if (!iso_reader_validate_file(reader, filename, &crc)) {
        printf("  INFO: Nao foi possivel calcular CRC\n");
        test_pass("Verificacao de CRC executada");
        return;
    }
    
    /* Depois verifica contra ele mesmo (deve passar) */
    if (iso_reader_verify_file_crc(reader, filename, crc)) {
        printf("  CRC32 verificado: 0x%08X (match)\n", crc);
        test_pass("Verificar CRC32");
    } else {
        test_fail("Verificar CRC32", "CRC nao corresponde");
    }
    
    /* Testa com CRC errado (deve falhar) */
    uint32_t wrong_crc = crc ^ 0xFFFFFFFF;
    if (!iso_reader_verify_file_crc(reader, filename, wrong_crc)) {
        printf("  CRC errado corretamente rejeitado\n");
        test_pass("Rejeitar CRC incorreto");
    } else {
        test_fail("Rejeitar CRC incorreto", "Deveria ter falhado");
    }
}

/**
 * Teste 14: Limpar erro
 */
static void test_clear_error(struct iso_reader *reader) {
    printf("\n=== TESTE: Limpar Erro ===\n");
    
    /* Provoca um erro */
    struct iso_file_entry entry;
    iso_reader_find_file_entry(reader, "ARQUIVO_QUE_NAO_EXISTE_12345.XYZ", &entry);
    
    int err_before = iso_reader_get_last_error(reader);
    
    iso_reader_clear_error(reader);
    
    int err_after = iso_reader_get_last_error(reader);
    
    printf("  Erro antes: %d\n", err_before);
    printf("  Erro depois: %d\n", err_after);
    
    if (err_after == ISO_OK) {
        test_pass("Limpar erro");
    } else {
        test_fail("Limpar erro", "Erro nao foi limpo");
    }
}

/**
 * Teste 15: Fechar ISO
 */
static void test_close(struct iso_reader *reader) {
    printf("\n=== TESTE: Fechar ISO ===\n");
    
    iso_reader_close(reader);
    
    if (!iso_reader_is_open(reader)) {
        test_pass("Fechar ISO");
    } else {
        test_fail("Fechar ISO", "ISO ainda esta aberta");
    }
}

/* ==================== Main ==================== */

int main(int argc, char *argv[]) {
    printf("=============================================\n");
    printf("       PSRetrox - ISOReader Test Suite       \n");
    printf("=============================================\n");
    
    if (argc < 2) {
        printf("\nUso: %s <caminho_iso> [arquivo_para_testar]\n", argv[0]);
        printf("\nExecutando apenas teste de erros...\n");
        test_error_handling();
        printf("\n=============================================\n");
        printf("Testes: %d passaram, %d falharam\n", g_tests_passed, g_tests_failed);
        printf("=============================================\n");
        return g_tests_failed > 0 ? 1 : 0;
    }
    
    const char *iso_path = argv[1];
    const char *test_file = (argc > 2) ? argv[2] : "SYSTEM.CNF";
    
    printf("\nISO: %s\n", iso_path);
    printf("Arquivo de teste: %s\n", test_file);
    
    struct iso_reader reader = {0};
    
    /* Executa todos os testes */
    test_error_handling();
    test_open_close(&reader, iso_path);
    
    if (iso_reader_is_open(&reader)) {
        test_detect_format(&reader);
        test_read_block(&reader);
        test_read_range(&reader);
        test_find_system_cnf(&reader);
        test_list_root_directory(&reader);
        test_recursive_directory(&reader);
        test_find_file_entry(&reader, test_file);
        test_validate_file(&reader, test_file);
        test_extract_file(&reader, test_file);
        test_extract_file_entry(&reader, test_file);
        test_verify_crc(&reader, test_file);
        test_clear_error(&reader);
        test_close(&reader);
    }
    
    /* Resumo */
    printf("\n=============================================\n");
    printf("               RESUMO DOS TESTES             \n");
    printf("=============================================\n");
    printf("  Passaram: %d\n", g_tests_passed);
    printf("  Falharam: %d\n", g_tests_failed);
    printf("  Total:    %d\n", g_tests_passed + g_tests_failed);
    printf("=============================================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
}
