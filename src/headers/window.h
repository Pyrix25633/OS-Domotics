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

#define INITIAL_SECONDS_OPEN    0
#define MAX_WINDOW_ARGUMENTS    2

// Macros - can be used instead of doing very small functions

//checks if values has changed
#define HAS_STATE_CHANGED(new_state) state!=new_state //Checks if the state has changed
#define HAS_PARENT_CHANGED(new_parent) parent_id!=new_parent //Checks if the parent has changed

//current seconds open
#define CURRENT_SECONDS_OPEN    time(NULL) - last_opened
#define LAST_SECONDS_OPEN       last_closed - last_opened

//no action switch command
#define NO_ACTION_SWITCH_OPEN SWITCH_LABEL(code)==SWITCH_OPEN && SWITCH_POSITION(code)==POSITION_OFF
#define NO_ACTION_SWITCH_CLOSE SWITCH_LABEL(code)==SWITCH_CLOSE && SWITCH_POSITION(code)==POSITION_OFF

//TODO values to substitute, just placeholders for now
#define MAX_WAITING 3
#define MIN_WAITING 1

/**
 * Main function of the Window Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * TODO: Determine all other possible exit values, add them every time you find out another error that requires complete
 * termination of the process can occur
 * 
 * @returns `OK`
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
void info_response();

/** 
 * Sets the attributes for the link response and performs actions if needed
 * @param code the command code to check
 */
void link_response(command_code_t code);

/**
 * Sets the attributes for the switch response and performs actions if needed
 * @param code the command code to check
 */
void switch_response(command_code_t code);

/**
 * Sets the attributes for the delete response and performs actions if needed
 * @param code the command code to check
 */
void delete_response(command_code_t code);

#endif
