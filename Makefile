# Compiler
CC = gcc
# The -std=c2x (C23) flag is only used to suppress pedantic warnings about binary constants (0b...)
# The project still compiles without it, it can be removed if it causes problems with the C version
CFLAGS = -MMD -MP -Isrc/headers -Wall -Wextra -pedantic -std=c2x -O2
LFLAGS = -lpthread
# Name of the file to be compiled and run
EXEC_NAME = bin/$(FILE)
# All devices source files for complete compilation
DEVICES_FILES = bulb.c window.c fridge.c hub.c timer.c manual_interaction.c controller.c
DEVICES_SRCS = $(addprefix src/, $(DEVICES_FILES))
# Their object files
DEVICES_OBJS = $(DEVICES_SRCS:src/%.c=bin/%.o)
# All devices executables
DEVICES_EXEC_NAMES = $(DEVICES_SRCS:src/%.c=bin/%)
# Generic files needed by any other main file
GENERIC_FILES = return_codes.c messages.c utils.c routing.c devices.c
GENERIC_SRCS = $(addprefix src/, $(GENERIC_FILES))
# Their compiled file
GENERIC_OBJS = $(GENERIC_SRCS:src/%.c=bin/%.o)
# All source files
ALL_SRCS = $(wildcard src/*.c)
# And their dependency files
ALL_DEPS = $(ALL_SRCS:src/%.c=bin/%.d)

# Tells "make" to not remove .o files, keeping them could reduce compile time if the .c has not changed
.SECONDARY: $(GENERIC_OBJS) $(DEVICES_OBJS)

# Compile, link and run a specific main file
default: bin/ ipc/ $(EXEC_NAME)
	./$(EXEC_NAME) $(ARGS)

# Linking step, need all object files
bin/%: bin/%.o $(GENERIC_OBJS)
	$(CC) -o $@ $^ $(LFLAGS)

# Link ncurses just for the Controller, this way the other binaries still build
bin/controller: LFLAGS += -lncurses

# Tells "make" to watch for dependency files, used to recompile if an used .h has changed, even if the .c has not
-include $(ALL_DEPS)
# Compilation step, need C file, not done if already compiled
bin/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# Build all executables
build: bin/ ipc/ $(DEVICES_EXEC_NAMES)

bin/:
	mkdir bin/

ipc/:
	mkdir ipc/

# Remove all compiled files, executables and IPC related files
clean:
	rm -f bin/*
	rm -f ipc/*

# Executes scenario, runs Controller which then reads if present the `./commands.scenario` file
run: | clean build
	./bin/controller $(ARGS)