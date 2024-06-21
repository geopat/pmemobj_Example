# Compiler
CC = gcc

# Compiler flags
CFLAGS = -std=c11 -g -pg

# Libraries to link against
LIBS = -lpmem -lpmemobj -pthread -latomic

# Source file
SRC = arrayExample.c #myexample.c

# Target executable
TARGET = arrayExample #myExample

# Default rule
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

# Clean rule
clean:
	rm -rf /mnt/pmem0/geopat/MYPOOL2
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
