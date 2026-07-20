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
#include <ncurses.h>

#define STDOUTERR_PIPE_NAME "./ipc/stdouterr.fifo"
#define STDOUTERR_BUFFER_SIZE 512

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
 * `UNABLE_TO_RESTORE_STDERR` if it was impossible to restore the standard streams after failing to redirect them,
 * `OK` otherwise
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

/**
 * Initializes the ncurses library, creates the windows and redirects the `stdout` and `stderr` threads,
 * if there is no `--no-ncurses` argument
 * 
 * @param argc Number of command-line arguments
 * @param argv Arguments vector
 * 
 * If errors occur, the function attempts to restore the original terminal mode and streams
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if there are errors with command-line arguments,
 * `UNABLE_TO_CREATE_PIPE` if the pipe could not be created or opened in read mode,
 * `UNABLE_TO_SET_FD_ATTR` if it could not be set back to blocking mode,
 * `UNABLE_TO_OPEN_PIPE` if it could not be opened in write mode,
 * `UNABLE_TO_CREATE_WINDOWS` if `ncurses` could not create the windows,
 * `UNABLE_TO_CREATE_THREAD` if the thread dedicated to reading the redirected streams could not be created,
 * `OK` otherwise
 */
error_code_t start_ncurses(int argc, char *argv[]);

/**
 * Redirects `stderr` to a named pipe
 * 
 * @returns `UNABLE_TO_CREATE_PIPE` if the pipe could not be created or opened in read mode,
 * `UNABLE_TO_SET_FD_ATTR` if it could not be set back to blocking mode,
 * `UNABLE_TO_OPEN_PIPE` if it could not be opened in write mode,
 * `OK` otherwise
 */
error_code_t redirect_stderr();

/**
 * Attempts to restore standard `stderr`, if it fails it exits with code `UNABLE_TO_RESTORE_STDERR`
 * 
 * @returns `UNABLE_TO_REMOVE_PIPE` if the redirect pipe could not be removed,
 * `OK` otherwise
 */
error_code_t restore_stderr();

/**
 * Creates ncurses windows dedicated to input and ouput
 * 
 * If something fails it attempts to restore streams
 * 
 * @returns `UNABLE_TO_CREATE_WINDOWS` if the library failed to create the windows,
 * `OK` otherwise
 */
error_code_t create_windows();

/**
 * Function that the `stderr` redirect thread executes
 * 
 * It reads from the redirection and prints to the ncurses terminal
 * 
 * If something fails there is not much that can be done as
 * the input-output interface itself is being manipulated,
 * so this function doesn't check for errors // TODO
 * 
 * @param arg Not used
 * 
 * @returns `NULL`
 */
void* stderr_routine(void *arg);

/**
 * Ends `ncurses` restoring the terminal to normal mode
 */
error_code_t end_ncurses();

#endif
