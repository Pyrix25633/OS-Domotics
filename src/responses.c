#include "responses.h"
#include "return_codes.h"

#include <stdio.h>

int parse_response(response_t *response, char *buffer, size_t size) {
    // TODO: Implement actual parsing

    return OK;
}

void format_response(response_t *response, char *buffer, size_t size) {
    sprintf(buffer, "%u %u %u\n", response->source, response->command_code, response->response_code);
}