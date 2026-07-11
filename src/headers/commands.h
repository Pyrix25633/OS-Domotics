/**
 * This file contains constant definitions and function declarations for commands
 */

#ifndef DOMOTICS_COMMANDS_H
#define DOMOTICS_COMMANDS_H

#include "device_types.h"

#include <stdlib.h>

typedef unsigned char command_code_t;

#define ARGUMENT_FLAG       0b100000
#define POSITION_MASK       0b000001
#define OFF                 0b000000
#define ON                  0b000001
#define SWITCH_MASK         0b000110
#define POWER               0b000010
#define OPEN                0b000100
#define CLOSE               0b000110
#define LINK_MASK           0b000011
#define LINK_CHANGE_PARENT  0b000001
#define LINK_NEW_CHILD      0b000010
#define LINK_DELETE_CHILD   0b000011
#define REGISTRY_MASK       0b000111
#define REGISTRY_BEGIN      0b000001
#define REGISTRY_END        0b000010
#define REGISTRY_DELAY      0b000101
#define REGISTRY_THERMOSTAT 0b000110
#define REGISTRY_PERCENTAGE 0b000111
#define COMMAND_MASK        0b111000
#define INFO                0b001000
#define SWITCH              0b010000
#define DELETE              0b011000
#define LINK                0b101000
#define REGISTRY            0b110000

#define IS_INFO(c)             (c & COMMAND_MASK) == INFO
#define IS_LINK(c)             (c & COMMAND_MASK) == LINK
#define LINK_SUBCOMMAND(c)     c & LINK_MASK
#define IS_SWITCH(c)           (c & COMMAND_MASK) == SWITCH
#define IS_REGISTRY(c)         (c & COMMAND_MASK) == REGISTRY
#define REGISTRY_SUBCOMMAND(c) c & REGISTRY_MASK
#define IS_DELETE(c)           (c & COMMAND_MASK) == DELETE
#define HAS_ARGUMENT(c)        (c & ARGUMENT_FLAG) == ARGUMENT_FLAG

typedef struct command_t {
    device_id_t destination;
    command_code_t command_code;
    unsigned short argument;
} command_t;

/**
 * Parses a string command and puts information in the command data structure
 * @param command Pointer to the data structure in which to put the parsed command
 * @param buffer Pointer to the string command to be parsed
 * @param size Size of the buffer
 * 
 * TODO: implement actual parsing
 * 
 * @returns `OK`
 */
int parse_command(command_t *command, char *buffer, size_t size);

/**
 * Creates a string command from information in the command data structure
 * @param command Pointer to the data structure containing the command to format as string
 * @param buffer Pointer to a string buffer in which the command will be put
 * @param size Size of the buffer
 */
void format_command(command_t *command, char *buffer, size_t size);

#endif