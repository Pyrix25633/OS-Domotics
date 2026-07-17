/**
 * This file contains type definitions, constant definitions and function declarations specific to the Timer
 */

#ifndef DOMOTICS_TIMER_H
#define DOMOTICS_TIMER_H

#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// Default values

#define MAX_TIMER_ARGUMENTS     4    //state, begin and end, the position 1 is not used by the timer
#define MINUTES_IN_A_DAY        1440 //begin and end are minutes from midnight, so they must be less than this
#define DEFAULT_BEGIN           0    //midnight, so any end is always greater than the default begin
#define DEFAULT_END             (MINUTES_IN_A_DAY - 1) //23:59, so any begin is always smaller than the default end

/**
 * Main function of the Timer Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 *
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_OPEN_PIPE` if the IPC pipes could not be opened,
 * `UNABLE_TO_CLOSE_PIPE` if the IPC pipes could not be closed,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 */
void handle_shutdown();

/**
 * Reads the pipe and identifies the command
 * @returns the error code to handle for the response
 */
error_code_t read_pipe();

/**
 * Writes to the pipe
 */
void write_pipe();

/**
 * Executes the command received from the pipe
 */
void execute_command();

/**
 * Sets the attributes for the info response
 */
void create_info_response();

/**
 * Sets the attributes for the link response and performs actions if needed
 */
void create_link_response();

/**
 * Sets the attributes for the registry response and performs actions if needed
 */
void create_registry_response();

/**
 * Sets the attributes for the switch response and performs actions if needed
 */
void create_switch_response();

#endif
