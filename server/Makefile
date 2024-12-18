# Compiler settings
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = 

# Cross-compilation settings
ifneq ($(CROSS_COMPILE),)
    CC = $(CROSS_COMPILE)gcc
endif

# Define the source and output files
SRC = aesdsocket.c
OBJ = aesdsocket.o
TARGET = aesdsocket

# Default target
all: $(TARGET)

# Rule to build the aesdsocket application
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Rule to build object files
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# Clean target to remove executable and object files
clean:
	rm -f $(TARGET) $(OBJ)

# Phony targets
.PHONY: all clean