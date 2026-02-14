# psretrox — PS2 Reverse Engineering Toolkit
# Pure C. Plan 9-style layout.
# Pipeline: ISO → ELF → R5900 decode → C translation

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -I include
LDFLAGS =

# ==================== Sources ====================

# Library sources (always compiled)
LIB_SRC = src/iso_reader.c src/file_utils.c src/assets.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# Tool sources — R5900 decompiler + recompiler (replaces old disasm.c/convert.c)
TOOL_SRC = tools/r5900_decompiler.c tools/recompiler.c
TOOL_OBJ = $(TOOL_SRC:.c=.o)

# Main
MAIN_SRC = src/main.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# Test
TEST_SRC = tests/test_iso_reader.c
TEST_OBJ = $(TEST_SRC:.c=.o)

# ==================== Targets ====================

.PHONY: all clean test run

# Default: build psretrox binary
all: psretrox.bin

# Main binary — links everything
psretrox.bin: $(MAIN_OBJ) $(LIB_OBJ) $(TOOL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Direct run: make run ISO=game.iso
run: psretrox.bin
	@if [ -z "$(ISO)" ]; then \
		echo "Uso: make run ISO=caminho/do/jogo.iso"; \
		exit 1; \
	fi
	./psretrox.bin $(ISO)

# Test binary — ISO reader tests
test_iso: $(TEST_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test: test_iso
	@echo "Run: ./test_iso <path_to_iso>"

# ==================== Compilation Rules ====================

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tools/%.o: tools/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ==================== Clean ====================

clean:
	rm -f $(LIB_OBJ) $(TOOL_OBJ) $(MAIN_OBJ) $(TEST_OBJ)
	rm -f psretrox.bin test_iso
	rm -f extracted.elf disasm_output.asm recompiled_output.c
