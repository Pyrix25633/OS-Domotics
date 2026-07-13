/**
 * This file contains constant definitions and function declarations for IPC commands and responses
 */

#ifndef DOMOTICS_MESSAGES_H
#define DOMOTICS_MESSAGES_H

#include "return_codes.h"
#include "device_types.h"
#include "sys/types.h"

typedef unsigned char command_code_t;

typedef struct command_t {
    device_id_t destination;
    command_code_t command_code;
    u_int16_t argument;
} command_t;

#define MAX_RESPONSE_ARGUMENTS 6 // Maximum is from a fridge info

typedef struct response_t {
    device_id_t source;
    command_code_t command_code;
    error_code_t response_code;
    u_int16_t arguments[MAX_RESPONSE_ARGUMENTS];
    size_t arguments_size; // Not present in the formatted response, set before formatting
} response_t;

#define COMMAND_ARGUMENT_FLAG   0b0100000
#define RESPONSE_ARGUMENTS_FLAG 0b1000000
#define POSITION_MASK           0b0000001
#define POSITION_OFF            0b0000000
#define POSITION_ON             0b0000001
#define SWITCH_MASK             0b0000110
#define SWITCH_POWER            0b0000010
#define SWITCH_OPEN             0b0000100
#define SWITCH_CLOSE            0b0000110
#define LINK_MASK               0b0000011
#define LINK_CHANGE_PARENT      0b0000001
#define LINK_NEW_CHILD          0b0000010
#define LINK_DELETE_CHILD       0b0000011
#define REGISTRY_MASK           0b0000111
#define REGISTRY_BEGIN          0b0000001
#define REGISTRY_END            0b0000010
#define REGISTRY_DELAY          0b0000101
#define REGISTRY_THERMOSTAT     0b0000110
#define REGISTRY_PERCENTAGE     0b0000111
#define COMMAND_MASK            0b1111000
#define INFO                    0b1001000
#define SWITCH                  0b0010000
#define DELETE                  0b0011000
#define LINK                    0b1101000
#define REGISTRY                0b1110000

#define IS_INFO(c)                (c & COMMAND_MASK) == INFO
#define IS_LINK(c)                (c & COMMAND_MASK) == LINK
#define LINK_SUBCOMMAND(c)        c & LINK_MASK
#define IS_SWITCH(c)              (c & COMMAND_MASK) == SWITCH
#define IS_REGISTRY(c)            (c & COMMAND_MASK) == REGISTRY
#define REGISTRY_SUBCOMMAND(c)    c & REGISTRY_MASK
#define IS_DELETE(c)              (c & COMMAND_MASK) == DELETE
#define HAS_COMMAND_ARGUMENT(c)   (c & COMMAND_ARGUMENT_FLAG) == COMMAND_ARGUMENT_FLAG
#define HAS_RESPONSE_ARGUMENTS(c) (c & RESPONSE_ARGUMENTS_FLAG) == RESPONSE_ARGUMENTS_FLAG

/**
 * Parses a string command and puts information in the command data structure
 * 
 * @param command Pointer to the data structure in which to put the parsed command
 * @param buffer Pointer to the string command to be parsed
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the string is not NULL-terminated and so the reading buffer was too short,
 * `COMMAND_FORMAT_ERROR` if there was an error in the format,
 * `OK` otherwise
 */
error_code_t parse_command(command_t *command, char *buffer, size_t size);

/**
 * Creates a string command from information in the command data structure
 * 
 * @param command Pointer to the data structure containing the command to format as string
 * @param buffer Pointer to a string buffer in which the command will be put
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the formatted command does not fit in the buffer,
 * `COMMAND_FORMAT_ERROR` if there was an error formatting the string,
 * `OK` otherwise
 */
error_code_t format_command(command_t *command, char *buffer, size_t size);

/**
 * Parses a string response and puts information in the response data structure
 * 
 * @param response Pointer to the data structure in which to put the parsed response
 * @param buffer Pointer to the string response to be parsed
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the string is not NULL-terminated and so the reading buffer was too short,
 * `RESPONSE_FORMAT_ERROR` if there was an error in the format,
 * `OK` otherwise
 */
error_code_t parse_response(response_t *response, char *buffer, size_t size);

/**
 * Creates a string response from information in the response data structure
 * 
 * @param response Pointer to the data structure containing the response to format as string
 * @param buffer Pointer to a string buffer in which the response will be put
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the formatted command does not fit in the buffer,
 * `RESPONSE_FORMAT_ERROR` if there was an error formatting the string,
 * `OK` otherwise
 */
error_code_t format_response(response_t *response, char *buffer, size_t size);

#endif
