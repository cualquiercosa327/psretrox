#include "../include/iso_reader.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Callback chamado para cada arquivo encontrado no diretório
void print_file_entry(const char *filename, void *userdata)
{
    int *count = (int *)userdata;
    (*count)++;
    printf("  [%03d] %s\n", *count, filename);
}

int main(int argc, char *argv[])
{
    char path[512];
    struct ISOReader reader;

    printf("===========================================\n");
    printf("       PSRetrox ISO Reader - Teste\n");
    printf("===========================================\n\n");

    // Pega caminho da ISO (argumento ou input)
    if (argc > 1)
    {
        strncpy(path, argv[1], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    else
    {
        printf("Digite o caminho para a ISO: ");
        if (!fgets(path, sizeof(path), stdin))
        {
            printf("Erro ao ler entrada.\n");
            return 1;
        }
        // Remove newline
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '\n')
            path[len - 1] = '\0';
    }

    printf("\nAbrindo: %s\n", path);

    // ============ TESTE 1: Abrir ISO ============
    printf("\n[TESTE 1] Abrindo arquivo ISO...\n");
    if (!ISOReader_init(&reader, path))
    {
        printf("ERRO: Nao foi possivel abrir a ISO.\n");
        printf("Verifique se o caminho esta correto.\n");
        return 1;
    }
    printf("OK: Arquivo aberto com sucesso!\n");

    // ============ TESTE 2: Verificar se está aberto ============
    printf("\n[TESTE 2] Verificando estado...\n");
    if (ISOReader_isOpen(&reader))
        printf("OK: ISOReader_isOpen() retornou TRUE\n");
    else
        printf("ERRO: ISOReader_isOpen() retornou FALSE\n");

    // ============ TESTE 3: Ler bloco 16 (Primary Volume Descriptor) ============
    printf("\n[TESTE 3] Lendo bloco 16 (Primary Volume Descriptor)...\n");
    uint8_t block[ISO_BLOCK_SIZE];
    size_t bytesRead = ISOReader_readBlock(&reader, 16, block, ISO_BLOCK_SIZE);
    
    if (bytesRead > 0)
    {
        printf("OK: %zu bytes lidos\n", bytesRead);
        
        // Mostra identificador do volume (bytes 1-5 devem ser "CD001")
        printf("Identificador: %.5s\n", &block[1]);
        
        // Mostra nome do volume (bytes 40-71)
        printf("Nome do Volume: ");
        for (int i = 40; i < 72; i++)
        {
            if (isprint(block[i]))
                putchar(block[i]);
        }
        printf("\n");
        
        // Mostra alguns bytes printáveis do bloco
        printf("Primeiros 200 caracteres printaveis:\n");
        int printed = 0;
        for (int i = 0; i < ISO_BLOCK_SIZE && printed < 200; i++)
        {
            if (isprint(block[i]))
            {
                putchar(block[i]);
                printed++;
            }
        }
        printf("\n");
    }
    else
    {
        printf("ERRO: Nao foi possivel ler o bloco 16\n");
    }

    // ============ TESTE 4: Buscar SYSTEM.CNF ============
    printf("\n[TESTE 4] Buscando SYSTEM.CNF...\n");
    char systemCnf[512];
    if (ISOReader_findSystemCNF(&reader, systemCnf, sizeof(systemCnf)))
    {
        printf("OK: SYSTEM.CNF encontrado!\n");
        printf("Conteudo:\n%s\n", systemCnf);
    }
    else
    {
        printf("INFO: SYSTEM.CNF nao encontrado (normal para algumas ISOs)\n");
    }

    // ============ TESTE 5: Listar diretório raiz ============
    printf("\n[TESTE 5] Listando arquivos do diretorio raiz...\n");
    int fileCount = 0;
    ISOReader_readDirectory(&reader, print_file_entry, &fileCount);
    printf("Total: %d arquivos/pastas encontrados\n", fileCount);

    // ============ TESTE 6: Ler intervalo de bytes ============
    printf("\n[TESTE 6] Testando leitura de intervalo (offset 0x8000, 64 bytes)...\n");
    uint8_t rangeBuffer[64];
    size_t rangeRead = ISOReader_readRange(&reader, 0x8000, 64, rangeBuffer);
    if (rangeRead > 0)
    {
        printf("OK: %zu bytes lidos\n", rangeRead);
        printf("Hex dump: ");
        for (size_t i = 0; i < rangeRead && i < 32; i++)
            printf("%02X ", rangeBuffer[i]);
        printf("...\n");
    }

    // ============ TESTE 7: Extrair arquivo (opcional) ============
    printf("\n[TESTE 7] Teste de extracao de arquivo\n");
    printf("Deseja extrair algum arquivo? (s/n): ");
    char resp[8];
    if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 's' || resp[0] == 'S'))
    {
        char filename[256];
        char output[256];
        
        printf("Nome do arquivo na ISO (ex: ICON0.PNG): ");
        if (fgets(filename, sizeof(filename), stdin))
        {
            size_t len = strlen(filename);
            if (len > 0 && filename[len - 1] == '\n')
                filename[len - 1] = '\0';

            printf("Caminho de saida (ex: ./extraido.bin): ");
            if (fgets(output, sizeof(output), stdin))
            {
                len = strlen(output);
                if (len > 0 && output[len - 1] == '\n')
                    output[len - 1] = '\0';

                printf("Extraindo '%s' para '%s'...\n", filename, output);
                if (ISOReader_extractFileByName(&reader, filename, output))
                    printf("OK: Arquivo extraido com sucesso!\n");
                else
                    printf("ERRO: Nao foi possivel extrair o arquivo\n");
            }
        }
    }

    // ============ FINALIZAÇÃO ============
    printf("\n[FINALIZANDO] Fechando ISO...\n");
    ISOReader_close(&reader);
    
    if (!ISOReader_isOpen(&reader))
        printf("OK: ISO fechada corretamente\n");

    printf("\n===========================================\n");
    printf("          Teste finalizado!\n");
    printf("===========================================\n");

    return 0;
}
