# Compiler settings
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = 

# Cross-compilation settings
ifneq ($(CROSS_COMPILE),)
    CC = $(CROSS_COMPILE)gcc
endif

# Define the source and output files
SRC = writer.c
OBJ = writer.o
EXEC = writer

# Default target
all: $(EXEC)

# Rule to build the writer application
$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $(EXEC) $(LDFLAGS)

# Rule to build object files
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# Clean target to remove executable and object files
clean:
	rm -f $(EXEC) $(OBJ)

# Phony targets
.PHONY: all clean
