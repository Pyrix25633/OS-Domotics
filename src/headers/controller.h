/**
 * This file contains type definitions, constant definitions and function declarations specific to the Controller
 */

#ifndef DOMOTICS_CONTROLLER_H
#define DOMOTICS_CONTROLLER_H

#include "devices.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include "routing.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

/**
 * Main function of the Controller Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * TODO: Determine all other possible exit values, add them every time you find out another error that requires complete
 * termination of the process can occur
 * 
 * @returns `UNABLE_TO_CREATE_PIPE` if the pipe where the Controller receives
 * responses from children could not be created and opened,
 * `OK`
 */
int main(int argc, char *argv[]);

/**
 * Creates and opens `"./ipc/<controller_id>_up.fifo"`
 * 
 * If an error occurs, it is printed and the function exits, as it's a critical non-solvable error
 */
void start_responses_fifo();

/**
 * Closes and removes `"./ipc/<controller_id>_up.fifo"`
 * 
 * @returns `UNABLE_TO_CLOSE_PIPE` if the pipe could not be closed,
 * `UNABLE_TO_REMOVE_PIPE` if the pipe could not be removed,
 * `OK` otherwise
 */
error_code_t end_responses_fifo();

/**
 * Handles the shutdown
 * 
 * @param error Error that caused the shutdown, if not `OK`
 */
void handle_shutdown(error_code_t error);

/**
 * Handles the shutdown caused by a `SIGTERM` signal
 */
void sigterm_handler();

#endif
