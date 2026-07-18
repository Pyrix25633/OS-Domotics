/**
 * This file contains constant definitions and function declarations for IPC commands and responses
 */

#ifndef DOMOTICS_MESSAGES_H
#define DOMOTICS_MESSAGES_H

#include "return_codes.h"
#include "devices.h"
#include "sys/types.h"

typedef u_int8_t command_code_t;

// The id is 32 bits, it can be at maximum 4294967295, so 10 digits
// The command code is 8 bits, it can be at maximum 255, so 3 digits
// The argument is 16 bits, it can be at maximum 65535, so 5 digits
// There are 2 spaces and the terminator char
// So maximum size is 10 + 3 + 5 + 2 + 1 = 21, round to 24 just to be sure
#define MAX_REQUEST_SIZE 24

typedef struct request_t {
    device_id_t destination;
    command_code_t command_code;
    u_int16_t argument;
} request_t;

#define MAX_RESPONSE_ARGUMENTS 6 // Maximum is from a fridge info

// The id is 32 bits, it can be at maximum 4294967295, so 10 digits
// The command code is 8 bits, it can be at maximum 255, so 3 digits
// The response code is 16 bits, it can be at maximum 65535, so 5 digits
// Each argument is 16 bits, it can be at maximum 65535, so 5 digits
// There are maximum 6 arguments, 2 + 5 = 7 spaces and the terminator char
// So maximum size is 10 + 3 + 5 + 5*6 + 7 = 55, round to 64 just to be sure
#define MAX_RESPONSE_SIZE 64

typedef struct response_t {
    device_id_t source;
    command_code_t command_code;
    error_code_t response_code;
    u_int16_t arguments[MAX_RESPONSE_ARGUMENTS];
    size_t arguments_size; // Not present in the formatted response, set before formatting
} response_t;

// Command codes (`0b` prefix indicates binary, it's an extension)

#define REQUEST_ARGUMENT_FLAG   0b0100000 // Responses to requests that have an argument should also contain that same argument
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
#define LINK_REMOVE_CHILD       0b0000011
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
#define NULL_COMMAND            0b0000000 // Used when the command could not be parsed but is needed in the response

// Command checking macros

#define IS_INFO(c)                (c & COMMAND_MASK) == INFO
#define IS_LINK(c)                (c & COMMAND_MASK) == LINK
#define LINK_SUBCOMMAND(c)        (c & LINK_MASK)
#define IS_SWITCH(c)              (c & COMMAND_MASK) == SWITCH
#define SWITCH_LABEL(c)           (c & SWITCH_MASK)
#define SWITCH_POSITION(c)        (c & POSITION_MASK)
#define IS_REGISTRY(c)            (c & COMMAND_MASK) == REGISTRY
#define REGISTRY_SUBCOMMAND(c)    (c & REGISTRY_MASK)
#define IS_DELETE(c)              (c & COMMAND_MASK) == DELETE
#define HAS_REQUEST_ARGUMENT(c)   (c & REQUEST_ARGUMENT_FLAG) == REQUEST_ARGUMENT_FLAG
#define HAS_RESPONSE_ARGUMENTS(c) (c & RESPONSE_ARGUMENTS_FLAG) == RESPONSE_ARGUMENTS_FLAG

// Response argument positions

#define STATE_ARGUMENT           0
#define REQUEST_ARGUMENT         0
#define PARENT_ID_ARGUMENT       0
#define OPEN_SECONDS_ARGUMENT    1
#define ON_SECONDS_ARGUMENT      1
#define DEVICE_TYPE_ARGUMENT     1
#define AUTOCLOSE_DELAY_ARGUMENT 2
#define BEGIN_ARGUMENT           2
#define FILL_PERCENTAGE_ARGUMENT 3
#define END_ARGUMENT             3
#define THERMOSTAT_ARGUMENT      4
#define TEMPERATURE_ARGUMENT     5

/**
 * Parses a string request and puts information in the request data structure
 * 
 * @param request Pointer to the data structure in which to put the parsed request
 * @param buffer Pointer to the string request to be parsed
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the string is not NULL-terminated and so the reading buffer was too short,
 * `REQUEST_FORMAT_ERROR` if there was an error in the format,
 * `OK` otherwise
 */
error_code_t parse_request(request_t *request, char *buffer, size_t size);

/**
 * Creates a string request from information in the request data structure
 * 
 * @param request Pointer to the data structure containing the request to format as string
 * @param buffer Pointer to a string buffer in which the request will be put
 * @param size Size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the string,
 * `OK` otherwise
 */
error_code_t format_request(request_t *request, char *buffer, size_t size);

/**
 * Parses a string response and puts information in the response data structure
 * 
 * For an `INFO` it ensures that there are at least two arguments, which should be the state and the time on/open
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
