# Makefile for PA-02: Re-engineering the ls Utility

CC       := gcc
CFLAGS   := -Wall -Wextra -std=gnu11 -g
LDFLAGS  :=

SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/ls

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
