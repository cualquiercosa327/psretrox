# psretrox — PS2 Reverse Engineering Toolkit
# Pure C. Plan 9-style layout.

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -I include
LDFLAGS =

# Capstone is optional — disasm module requires it
# Install: sudo apt install libcapstone-dev
CAPSTONE_CFLAGS := $(shell pkg-config --cflags capstone 2>/dev/null)
CAPSTONE_LIBS   := $(shell pkg-config --libs   capstone 2>/dev/null)

# ==================== Sources ====================

# Library sources (always compiled)
LIB_SRC = src/iso_reader.c src/file_utils.c src/assets.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# Tool sources (compiled only when Capstone is available)
TOOL_SRC = tools/disasm.c tools/convert.c
TOOL_OBJ = $(TOOL_SRC:.c=.o)

# Main
MAIN_SRC = src/main.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# Test
TEST_SRC = tests/test_iso_reader.c
TEST_OBJ = $(TEST_SRC:.c=.o)

# ==================== Targets ====================

.PHONY: all clean test

# Default: build psretrox binary
all: psretrox.bin

# Main binary — links everything
psretrox.bin: $(MAIN_OBJ) $(LIB_OBJ) $(TOOL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(CAPSTONE_LIBS)

# Test binary — ISO reader tests (no Capstone needed)
test_iso: $(TEST_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test: test_iso
	@echo "Run: ./test_iso <path_to_iso>"

# ==================== Compilation Rules ====================

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tools/disasm.o: tools/disasm.c
	$(CC) $(CFLAGS) $(CAPSTONE_CFLAGS) -c -o $@ $<

tools/%.o: tools/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ==================== Clean ====================

clean:
	rm -f $(LIB_OBJ) $(TOOL_OBJ) $(MAIN_OBJ) $(TEST_OBJ)
	rm -f psretrox.bin test_iso
