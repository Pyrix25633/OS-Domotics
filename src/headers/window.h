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

// Default values

#define INITIAL_SECONDS_OPEN    0
#define MAX_WINDOW_ARGUMENTS    2

//TODO values to substitute, just placeholders for now
#define UNEXPECTED_COMMAND 0x11

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
 */
error_code_t read_pipe();

/**
 * Executes the command received from the pipe
 */
void execute_command();

/**
 * TODO
 */
void info_response();

/**
 * TODO
 */
void link_response(command_code_t code);

#endif
