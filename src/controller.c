#include "controller.h"
#include "devices.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    /*char buffer[15] = "17 49 121";
    command_t command;
    printf("%u\n", parse_command(&command, buffer, 15));
    printf("destination: %u, command_code: %u, argument: %u\n", command.destination, command.command_code, command.argument);
    printf("%u\n", format_command(&command, buffer, 15));
    printf("%s", buffer);*/

    /*char buffer[20];
    response_t response;
    response.source = 15;
    response.command_code = INFO;
    response.response_code = OK;
    response.arguments[0] = 18;
    response.arguments[1] = 5;
    response.arguments_size = 2;
    printf("%u\n", format_response(&response, buffer, 14));
    printf("%s\n", buffer);
    char test[20] = "15 72 0";
    printf("%u\n", parse_response(&response, buffer, 20));*/

    return OK;
}