#include "controller.h"
#include "device_types.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    char buffer[15] = "17 49 121";
    command_t command;
    printf("%u\n", parse_command(&command, buffer, 15));
    printf("destination: %u, command_code: %u, argument: %u\n", command.destination, command.command_code, command.argument);
    printf("%u\n", format_command(&command, buffer, 15));
    printf("%s", buffer);

    return OK;
}