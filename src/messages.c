#include "messages.h"
#include "return_codes.h"
#include "utils.h"

#define _XOPEN_SOURCE 800
#include <stdio.h>
#include <string.h>
#include <limits.h>

error_code_t parse_command(command_t *command, char *buffer, size_t size) {
    size_t length = string_length(buffer, size);
    if(length == size) {
        return BUFFER_TOO_SHORT;
    }

    char *token = strtok(buffer, " ");
    if(token == NULL) {
        return COMMAND_FORMAT_ERROR;
    }
    int destination = string_to_unsigned(token);
    if(IS_RETURN_ERROR(destination)) {
        return COMMAND_FORMAT_ERROR;
    }

    token = strtok(NULL, " ");
    if(token == NULL) {
        return COMMAND_FORMAT_ERROR;
    }
    int command_code = string_to_unsigned(token);
    if(IS_RETURN_ERROR(command_code)) {
        return COMMAND_FORMAT_ERROR;
    }

    command->destination = destination;
    command->command_code = command_code;

    if(HAS_ARGUMENT(command_code)) {
        token = strtok(NULL, " ");
        if(token == NULL) {
            return COMMAND_FORMAT_ERROR;
        }
        int argument = string_to_unsigned(token);
        if(IS_RETURN_ERROR(argument)) {
            return COMMAND_FORMAT_ERROR;
        }
        command->argument = argument;
    }

    return OK;
}

error_code_t format_command(command_t *command, char *buffer, size_t size) {
    int length;
    if(HAS_ARGUMENT(command->command_code)) {
        length = snprintf(buffer, size, "%u %u %u\n", command->destination, command->command_code, command->argument);
    } else {
        length = snprintf(buffer, size, "%u %u\n", command->destination, command->command_code);
    }
    if(length >= size) {
        return BUFFER_TOO_SHORT;
    }
    if(length < 0) {
        return COMMAND_FORMAT_ERROR;
    }
    return OK;
}

error_code_t parse_response(response_t *response, char *buffer, size_t size) {
    // TODO: Implement actual parsing

    return OK;
}

error_code_t format_response(response_t *response, char *buffer, size_t size) {
    int length = snprintf(buffer, size, "%u %u %u\n", response->source, response->command_code, response->response_code);
    if(length >= size) {
        return BUFFER_TOO_SHORT;
    }
    if(length < 0) {
        return RESPONSE_FORMAT_ERROR;
    }
    return OK;
}