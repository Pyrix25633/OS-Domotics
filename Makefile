# Compiler
CC = gcc
# TODO: add extra flags for specific libraries, add -Wall -Wextra -pedantic
CFLAGS = -Isrc/headers
# Name of the file to be compiled and run
EXEC_NAME = bin/$(FILE)
# All devices source files for complete compilation
DEVICES_FILES = bulb.c window.c fridge.c controller.c hub.c timer.c
DEVICES_SRCS = $(addprefix src/, $(DEVICES_FILES))
# Their object files
DEVICES_OBJS = $(DEVICES_SRCS:src/%.c=bin/%.o)
# All devices executables
DEVICES_EXEC_NAMES = $(DEVICES_SRCS:src/%.c=bin/%)
# Generic files needed by any other main file
GENERIC_FILES = return_codes.c commands.c responses.c
GENERIC_SRCS = $(addprefix src/, $(GENERIC_FILES))
# Their compiled file
GENERIC_OBJS = $(GENERIC_SRCS:src/%.c=bin/%.o)

# Tells "make" to not remove .o files, keeping them could reduce compile time if the .c has not changed
.SECONDARY: $(GENERIC_OBJS) $(DEVICES_OBJS)

# Compile, link and run a specific main file
default: $(EXEC_NAME)
	./$< $(ARGS)

# Linking step, need all object files
bin/%: bin/%.o $(GENERIC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compilation step, need C file, not done if already compiled
bin/%.o : src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build all executables
build: $(DEVICES_EXEC_NAMES)

# Remove all compiled files, executables and IPC related files
clean:
	rm bin/*
	rm ipc/*

# TODO: execute a scenario
run:
	echo "Not implemented yet"