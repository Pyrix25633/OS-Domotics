/**
 * This file contains constant definitions and function declarations for responses
 */

#ifndef DOMOTICS_RESPONSES_H
#define DOMOTICS_RESPONSES_H

#include "device_types.h"
#include "commands.h"

typedef unsigned char response_code_t;

typedef struct response_t {
    device_id_t source;
    command_code_t command_code;
    response_code_t response_code;
    //TODO: determine how to attach info of device
} response_t;

/**
 * Parses a string response and puts information in the response data structure
 * @param response Pointer to the data structure in which to put the parsed response
 * @param buffer Pointer to the string response to be parsed
 * @param size Size of the buffer
 * 
 * TODO: implement actual parsing
 * 
 * @returns `OK`
 */
int parse_response(response_t *response, char *buffer, size_t size);

/**
 * Creates a string response from information in the response data structure
 * @param response Pointer to the data structure containing the response to format as string
 * @param buffer Pointer to a string buffer in which the response will be put
 * @param size Size of the buffer
 */
void format_response(response_t *response, char *buffer, size_t size);

#endif