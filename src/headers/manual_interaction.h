/**
 * This file contains type definitions, constant definitions and function declarations specific to the Manual Interaction
 */

#ifndef DOMOTICS_MANUAL_INTERACTION_H
#define DOMOTICS_MANUAL_INTERACTION_H

#include "devices.h"
#include "messages.h"
#include "return_codes.h"
#include "utils.h"
#include "routing.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

/**
 * Main function of the Manual Interaction Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * @returns `INVALID_TARGET_ID` if the target ID is missing or has an invalid format,
 * `INVALID_COMMAND` if the command is not between the expected ones or is missing,
 * `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `DEVICE_NOT_FOUND` if the target device does not exist,
 * `UNABLE_TO_OPEN_FILE` if the registry file could not be opened not because absent,
 * `REGISTRY_FORMAT_ERROR` if the registry file doesn't have the expected format,
 * `DEVICE_TYPE_MISMATCH` if the command does not match with the device type,
 * `UNABLE_TO_OPEN_PIPE` if the device pipe could not be opened,
 * `BUFFER_TOO_SHORT` or `REQUEST_FORMAT_ERROR` if the request could not be formatted,
 * `UNABLE_TO_WRITE_PIPE` if the request could not be sent,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Parses a command-line user command
 * 
 * @param user_command Struct where command data will be put
 * @param argc Number of string arguments
 * @param argv Arguments vector
 * 
 * @returns `INVALID_TARGET_ID` if the target ID is missing or has an invalid format,
 * `INVALID_COMMAND` if the command is not between the expected ones or is missing,
 * `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `OK` otherwise
 */
error_code_t parse_user_command(user_command_t *user_command, int argc, char *argv[]);

/**
 * Parses a command-line switch command
 * 
 * @param user_command Struct where command data will be put
 * @param argc Number of string arguments
 * @param argv Arguments vector
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `OK` otherwise
 */
error_code_t parse_switch_command(user_command_t *user_command, int argc, char *argv[]);

/**
 * Parses a command-line registry set command
 * 
 * @param user_command Struct where command data will be put
 * @param argc Number of string arguments
 * @param argv Arguments vector
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `OK` otherwise
 */
error_code_t parse_set_command(user_command_t *user_command, int argc, char *argv[]);

/**
 * Checks the user command for semantic errors, mismatch between command and device type
 * 
 * @param user_command The user command
 * @param type The device type
 * 
 * @returns `DEVICE_TYPE_MISMATCH` if the command doesn't match the device type,
 * `OK` otherwise
 */
error_code_t check_user_command(user_command_t *user_command, device_type_t type);

/**
 * Opens the pipe where to send commands to the target device
 * 
 * @param target Target device ID
 * 
 * @returns `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened,
 * `OK` otherwise
 */
void open_device_pipe(device_id_t target);

#endif
