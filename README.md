# psretrox - PlayStation 2 Decompilation & Recompilation Tool

## [EN] Project Overview & Roadmap

### Project Status & Authorship
This project was originally started by [joacolns](https://github.com/joacolns), then forked and continued by [cualquiercosa327](https://github.com/cualquiercosa327), and is now maintained and expanded by [krplab](https://github.com/krplab). The goal is to continue the PSRetrox project in my own way, keeping the name, but evolving the architecture, codebase, and integration with [ps2-recomp](https://github.com/ran-j/PS2Recomp) as a complementary tool.

### What is PSRetrox?
PSRetrox is a tool for reverse engineering PlayStation 2 games, focused on extracting, processing, and converting game files to enable native PC ports. It includes tools for decompiling, decoding, and extracting assets such as audio, 3D models, and code from PS2 ISOs.

### Roadmap: Migration to C & New Structure
- The project is being migrated from C++ to pure C for greater portability and maintainability.
- The folder structure will be modular and minimal, inspired by Plan 9:

```
/psretrox/
├── include/      # All headers (iso.h, elf.h, mips.h, runtime.h, assets.h, util.h)
├── src/          # All implementations (iso.c, elf.c, mips.c, runtime.c, assets.c, util.c, main.c)
├── tools/        # Optional: debug/conversion tools
├── tests/        # Unit and integration tests
├── out/          # Extracted files, logs, generated C code
```

- The pipeline will integrate:
	1. Extraction of ELF and assets from ISO (using ISOReader in C)
	2. Asset extraction (audio, 3D, etc.)

	3. Recompilation of ELF to C (adapting ps2-recomp pipeline to C)
	4. Minimal runtime in C for execution on PC

### Progress (2026-01)
- ISO extraction: 80%
- Asset extraction (audio/3D): 40%
- ELF parsing: 30%
- MIPS R5900 decoder/recompiler: 20%
- Runtime: 10%
- Integration with ps2-recomp: 10%

### Features
- Reverse engineering of PS2 ISOs
- Extraction and decoding of audio, 3D models, and other assets
- Recompilation of MIPS assembly to C
- Modular, portable, and testable codebase
- (Planned) Minimal runtime for running recompiled code on PC

### Contributing
Contributions are welcome! Please follow modern C practices, document complex logic, and ensure you do not break existing functionality. Open issues or pull requests as needed.


### [EN/PT-BR] Recent Updates

:calendar: **Date:** 11/02

- All code now uses snake_case naming for consistency and maintainability.
- ISOReader fully refactored: supports fragmented files (extents), subdirectory traversal, CRC32 validation, and robust error handling.
- All compilation warnings (including path truncation in recursive directory reading) have been fixed for maximum portability.
- The test suite was expanded and moved, now covering all ISOReader features: error handling, extraction, validation, recursive navigation, and edge cases.
- The codebase is modular, portable, and ready for further integration with audio, 3D, and ELF modules.

---

## [PT-BR] Visão Geral & Planejamento

### Status do Projeto & Histórico de Forks
O projeto foi iniciado por [joacolns](https://github.com/joacolns), depois continuado por [cualquiercosa327](https://github.com/cualquiercosa327), e agora mantido e expandido por [krplab](https://github.com/krplab). Pretendo dar continuidade ao psretrox do meu jeito, mantendo o nome, mas evoluindo a arquitetura, migrando para C e integrando o [ps2-recomp](https://github.com/ran-j/PS2Recomp) como complemento.

### O que é o PSRetrox?
Ferramenta para engenharia reversa de jogos de PlayStation 2, focada em extrair, processar e converter arquivos de jogos para viabilizar ports nativos no PC. Inclui ferramentas para decompilar, decodificar e extrair assets (áudio, modelos 3D, código) de ISOs de PS2.

### Planejamento: Migração para C & Nova Estrutura
- O projeto está sendo migrado de C++ para C puro, visando portabilidade e manutenção.
- A estrutura de pastas será modular e minimalista, inspirada no Plan 9:

```
/psretrox/
├── include/      # Headers (iso.h, elf.h, mips.h, runtime.h, assets.h, util.h)
├── src/          # Implementações (iso.c, elf.c, mips.c, runtime.c, assets.c, util.c, main.c)
├── tools/        # Ferramentas opcionais de debug/conversão
├── tests/        # Testes unitários e de integração
├── out/          # Arquivos extraídos, logs, código C gerado
```

- O pipeline integrará:
	1. Extração de ELF e assets da ISO (ISOReader em C)
	2. Extração de assets (áudio, 3D, etc.)
	3. Recompilação do ELF para C (adaptando pipeline do ps2-recomp para C)
	4. Runtime mínimo em C para execução no PC

### Progresso (2026-01)
- Extração de ISO: 80%
- Extração de assets (áudio/3D): 40%
- Parsing de ELF: 30%
- Decoder/recompilador MIPS R5900: 20%
- Runtime: 10%
- Integração com ps2-recomp: 10%

### Funcionalidades
- Engenharia reversa de ISOs de PS2
- Extração e decodificação de áudio, modelos 3D e outros assets
- Recompilação de assembly MIPS para C
- Código modular, portável e testável
- (Planejado) Runtime mínimo para rodar código recompilado no PC

### Contribuindo
Contribuições são bem-vindas! Siga boas práticas de C, documente lógicas complexas e garanta que não quebre funcionalidades existentes. Abra issues ou pull requests conforme necessário.

Atualizações Recentes

:calendar: **Data:** 11/02

* Todo o código agora utiliza nomenclatura em **snake_case**, garantindo maior consistência e manutenibilidade.
* O **ISOReader** foi completamente refatorado: agora oferece suporte a arquivos fragmentados (extents), navegação por subdiretórios, validação CRC32 e tratamento robusto de erros.
* Todos os *warnings* de compilação foram corrigidos (incluindo truncamento de caminhos na leitura recursiva de diretórios), assegurando máxima portabilidade.
* A suíte de testes foi expandida e reorganizada, passando a cobrir todas as funcionalidades do ISOReader: tratamento de erros, extração, validação, navegação recursiva e casos extremos (*edge cases*).
* A base de código está modular, portátil e pronta para futuras integrações com módulos de áudio, 3D e ELF.

Se quiser, posso:

* padronizar o texto para **CHANGELOG**,
* gerar uma versão **EN ↔ PT-BR lado a lado**, ou
* adaptar para **README.md** ou **release notes**.