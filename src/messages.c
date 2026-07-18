#define _XOPEN_SOURCE 700

#include "messages.h"
#include "return_codes.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

error_code_t parse_request(request_t *request, char *buffer, size_t size) {
    // Check that the string is NULL-terminated, if not the buffer was too short
    size_t length = string_length(buffer, size);
    if(length == size) {
        return BUFFER_TOO_SHORT;
    }

    // Parse destination
    char *last;
    char *token = strtok_r(buffer, " ", &last);
    if(token == NULL) {
        return REQUEST_FORMAT_ERROR;
    }
    int destination = string_to_unsigned(token);
    if(IS_RETURN_ERROR(destination)) {
        return REQUEST_FORMAT_ERROR;
    }

    // Parse command code
    token = strtok_r(NULL, " ", &last);
    if(token == NULL) {
        return REQUEST_FORMAT_ERROR;
    }
    int command_code = string_to_unsigned(token);
    if(IS_RETURN_ERROR(command_code)) {
        return REQUEST_FORMAT_ERROR;
    }

    request->destination = destination;
    request->command_code = command_code;

    // Parse command argument if present
    if(HAS_REQUEST_ARGUMENT(command_code)) {
        token = strtok_r(NULL, " ", &last);
        if(token == NULL) {
            return REQUEST_FORMAT_ERROR;
        }
        int argument = string_to_unsigned(token);
        if(IS_RETURN_ERROR(argument)) {
            return REQUEST_FORMAT_ERROR;
        }
        request->argument = argument;
    }

    return OK;
}

error_code_t format_request(request_t *request, char *buffer, size_t size) {
    int length;
    if(HAS_REQUEST_ARGUMENT(request->command_code)) {
        length = snprintf(buffer, size, "%u %u %u", request->destination, request->command_code, request->argument);
    } else {
        length = snprintf(buffer, size, "%u %u", request->destination, request->command_code);
    }
    if(length >= (int)size) {
        return BUFFER_TOO_SHORT;
    }
    if(length < 0) {
        return REQUEST_FORMAT_ERROR;
    }
    return OK;
}

error_code_t parse_response(response_t *response, char *buffer, size_t size) {
    // Check that the string is NULL-terminated, if not the buffer was too short
    size_t length = string_length(buffer, size);
    if(length == size) {
        return BUFFER_TOO_SHORT;
    }

    // Parse source
    char *last;
    char *token = strtok_r(buffer, " ", &last);
    if(token == NULL) {
        return REQUEST_FORMAT_ERROR;
    }
    int source = string_to_unsigned(token);
    if(IS_RETURN_ERROR(source)) {
        return REQUEST_FORMAT_ERROR;
    }

    // Parse command code
    token = strtok_r(NULL, " ", &last);
    if(token == NULL) {
        return REQUEST_FORMAT_ERROR;
    }
    int command_code = string_to_unsigned(token);
    if(IS_RETURN_ERROR(command_code)) {
        return REQUEST_FORMAT_ERROR;
    }

    // Parse response code
    token = strtok_r(NULL, " ", &last);
    if(token == NULL) {
        return REQUEST_FORMAT_ERROR;
    }
    int response_code = string_to_unsigned(token);
    if(IS_RETURN_ERROR(response_code)) {
        return REQUEST_FORMAT_ERROR;
    }

    response->source = source;
    response->command_code = command_code;
    response->response_code = response_code;

    // Parse arguments if it's a successful response and should have arguments
    if(HAS_RESPONSE_ARGUMENTS(command_code) && response->response_code == OK) {
        unsigned i = 0;
        int parsed;
        token = strtok_r(NULL, " ", &last);
        if(token == NULL) {
            return RESPONSE_FORMAT_ERROR;
        }
        do {
            parsed = string_to_unsigned(token);
            if(IS_RETURN_ERROR(parsed)) {
                return RESPONSE_FORMAT_ERROR;
            }
            response->arguments[i] = parsed;
            i++;
            token = strtok_r(NULL, " ", &last);
        } while(token != NULL && i < MAX_RESPONSE_ARGUMENTS);
        response->arguments_size = i;
    }

    if(IS_INFO(response->command_code) && response->arguments_size < 2) {
        return RESPONSE_FORMAT_ERROR;
    }

    return OK;
}

error_code_t format_response(response_t *response, char *buffer, size_t size) {
    // Format first 3 fields
    int length = snprintf(buffer, size, "%u %u %u", response->source, response->command_code, response->response_code);
    if(length >= (int)size) {
        return BUFFER_TOO_SHORT;
    }
    if(length < 0) {
        return RESPONSE_FORMAT_ERROR;
    }

    // Format response arguments
    int remaining = size - length;
    int position = length;
    for(unsigned i = 0; i < response->arguments_size; i++) {
        length = snprintf(&buffer[position], remaining, " %u", response->arguments[i]);
        if(length < 0) {
            return RESPONSE_FORMAT_ERROR;
        }
        remaining -= length;
        if(remaining <= 0) {
            return BUFFER_TOO_SHORT;
        }
        position += length;
    }

    // Terminate the string
    if(remaining < 1) {
        return BUFFER_TOO_SHORT;
    }
    buffer[position] = '\0';

    return OK;
}