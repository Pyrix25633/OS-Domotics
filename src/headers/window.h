/**
 * This file contains type definitions, constant definitions and function declarations specific to the Window
 */

#ifndef DOMOTICS_WINDOW_H
#define DOMOTICS_WINDOW_H

#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// Default values

#define MAX_WINDOW_ARGUMENTS    2

// Macros - can be used instead of doing very small functions

//current seconds open
#define CURRENT_SECONDS_OPEN    time(NULL) - last_opened
#define LAST_SECONDS_OPEN       last_closed - last_opened

//no action switch command
#define NO_ACTION_SWITCH_OPEN SWITCH_LABEL(request.command_code)==SWITCH_OPEN && SWITCH_POSITION(request.command_code)==POSITION_OFF
#define NO_ACTION_SWITCH_CLOSE SWITCH_LABEL(request.command_code)==SWITCH_CLOSE && SWITCH_POSITION(request.command_code)==POSITION_OFF

/**
 * Main function of the Window Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_OPEN_PIPE` if the IPC pipes could not be opened,
 * `UNABLE_TO_CLOSE_PIPE` if the IPC pipes could not be closed,
 * `UNABLE_TO_SET_SIGHANDLER` if a signal handler could not be set,
 * `UNEXPECTED_SHUTDOWN` if it received `SIGTERM` or `SIGINT`,
 * `UNEXPECTED_END_OF_FILE` if the pipe was closed by the parent,
 * `BROKEN_PIPE` if it received `SIGPIPE`,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 * 
 * @param error The error to handle
 */
void handle_shutdown(error_code_t error);

/**
 * Handles the shutdown caused by a `SIGTERM` or `SIGINT` signal
 * @param sig_num Signal number, unused
 */
void sigterm_handler(int sig_num);

/**
 * Handles the shutdown caused by a `SIGPIPE` signal
 * @param sig_num Signal number, unused
 */
void sigpipe_handler(int sig_num);

/**
 * Reads the pipe and identifies the command
 * 
 * @returns The error code to handle for the response
 */
error_code_t read_pipe();

/**
 * Writes to the pipe
 */
void write_pipe();

/**
 * Executes the command received from the pipe
 * @returns An error code to do the exit
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
