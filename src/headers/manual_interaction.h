/**
 * This file contains type definitions, constant definitions and function declarations specific to the Manual Interaction
 */

#ifndef DOMOTICS_MANUAL_INTERACTION_H
#define DOMOTICS_MANUAL_INTERACTION_H

#include "devices.h"
#include "messages.h"
#include "return_codes.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/**
 * Main function of the Manual Interaction Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * TODO: Determine all other possible exit values, add them every time you find out another error that requires complete
 * termination of the process can occur
 * 
 * @returns `INVALID_TARGET_ID` if the target ID is missing or has an invalid format,
 * `INVALID_COMMAND` if the command is not between the expected ones or is missing,
 * `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
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

#endif
