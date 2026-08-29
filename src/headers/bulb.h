/**
 * This file contains type definitions, constant definitions and function declarations specific to the Bulb
 */

#ifndef DOMOTICS_BULB_H
#define DOMOTICS_BULB_H

#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// Default values

#define MAX_BULB_ARGUMENTS      2

// Macros - can be used instead of doing very small functions

//current seconds on / duration of the last time it was on
#define CURRENT_SECONDS_ON      time(NULL) - last_turned_on
#define LAST_SECONDS_ON         last_turned_off - last_turned_on

/**
 * Main function of the Bulb Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 *
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_SET_SIGHANDLER` if a signal handler could not be set,
 * `UNABLE_TO_OPEN_PIPE` if the IPC pipes could not be opened,
 * `UNABLE_TO_CLOSE_PIPE` if the IPC pipes could not be closed,
 * `UNEXPECTED_END_OF_FILE` if the parent closed the pipe where the requests are received,
 * `UNEXPECTED_SHUTDOWN` if it received `SIGTERM` or `SIGINT`,
 * `BROKEN_PIPE` if it received `SIGPIPE`,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 * @param error The error code that caused the shutdown, returned if the cleanup succeeds
 */
void handle_shutdown(error_code_t error);

/**
 * Handles the shutdown caused by a `SIGTERM` or `SIGINT` signal
 * @param sig_num Number of the received signal, unused
 */
void sigterm_handler(int sig_num);

/**
 * Handles the shutdown caused by a `SIGPIPE` signal
 * @param sig_num Number of the received signal, unused
 */
void sigpipe_handler(int sig_num);

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
 * @returns the error code that occurred while reading the request, `OK` otherwise
 */
error_code_t execute_command();

/**
 * Sets the attributes for the info response
 */
void create_info_response();

/**
 * Sets the attributes for the link response and performs actions if needed
 */
void create_link_response();

/**
 * Sets the attributes for the switch response and performs actions if needed
 */
void create_switch_response();

#endif
