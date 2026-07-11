#include "commands.h"
#include "return_codes.h"

#include <stdio.h>

int parse_command(command_t *command, char *buffer, size_t size) {
    // TODO: Parse the command

    return OK;
}

void format_command(command_t *command, char *buffer, size_t size) {
    if(HAS_ARGUMENT(command->command_code)) {
        sprintf(buffer, "%u %u %u\n", command->destination, command->command_code, command->argument);
    } else {
        sprintf(buffer, "%u %u\n", command->destination, command->command_code);
    }
}