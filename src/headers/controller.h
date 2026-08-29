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
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <ncurses.h>
#include <stdarg.h>
#include <errno.h>
#include <stdatomic.h>

#define STDERR_PIPE_NAME     "./ipc/stderr.fifo"
#define STDERR_BUFFER_SIZE   512
#define USER_BUFFER_SIZE     64
#define RESPONSE_STATUS_SIZE 128
#define USER_MESSAGE_SIZE    256
#define CHILD_ERROR_SIZE     32
#define SCENARIO_FILE_NAME   "./commands.scenario"
#define MAIN_OFF             0
#define MAIN_ON              1

#define CHECK_NO_OTHER_ARGUMENTS(last) strtok_r(NULL, " ", &last) == NULL ? OK : INVALID_COMMAND_ARGUMENT

/**
 * Main function of the Controller Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * @returns the response code of the last handled command,
 * or one of the codes below if the shutdown was caused by a fatal error:
 * `UNABLE_TO_CREATE_PIPE` if the pipe where the Controller receives
 * responses from children could not be created and opened,
 * `UNABLE_TO_SET_SIGHANDLER` if a signal handler could not be set,
 * `UNABLE_TO_RESTORE_STDERR` if it was impossible to restore `stderr` after failing to redirect it,
 * `UNABLE_TO_CREATE_THREAD` if the thread dedicated to reading responses could not be created,
 * `UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked,
 * `UNABLE_TO_READ_PIPE` if the responses pipe could not be read,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Parses, checks and execute a user command
 * 
 * User for input from both terminal and file
 * 
 * Prints errors, returns error code for exit code
 * 
 * @param user_command Struct where command data will be put
 * @param string The string command
 * 
 * @returns `INVALID_TARGET_ID` if the target ID is missing or has an invalid format,
 * `INVALID_COMMAND` if the command is not between the expected ones or is missing,
 * `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `UNABLE_TO_LOCK_MUTEX` if data mutex could not be locked,
 * `DEVICE_NOT_FOUND` if there is no device with the specified target ID,
 * `ROUTE_NOT_FOUND` if the target device has been removed from the routing table before the request could be sent,
 * `DEVICE_TYPE_MISMATCH` if the command is not compatible with the target device,
 * `SYSTEM_OFF` if the system is off and the command is not a switch main,
 * `CANNOT_ADD_TO_PARENT` if the parent wouldn't change or is a Timer already with a child,
 * `LINKING_PARENT_TO_CHILD` if the link would create cycles,
 * `UNABLE_TO_UNLOCK_MUTEX` if data mutex could not be locked,
 * `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the request,
 * `UNABLE_TO_WRITE_PIPE` if the write failed,
 * `UNABLE_TO_CREATE_PIPE` if the pipe where the device will receive commands could not be created,
 * `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened in write mode,
 * `UNABLE_TO_ALLOCATE_HEAP` if the routing information could not be inserted,
 * `OK` otherwise
 */
error_code_t process_user_command(user_command_t *user_command, char *string);

/**
 * Parses a user command
 * 
 * @param user_command Struct where command data will be put
 * @param string The string command
 * 
 * @returns `INVALID_TARGET_ID` if the target ID is missing or has an invalid format,
 * `INVALID_COMMAND` if the command is not between the expected ones or is missing,
 * `INVALID_COMMAND_ARGUMENT` if any of the arguments is missing or doesn't have a valid format,
 * `OK` otherwise
 */
error_code_t parse_user_command(user_command_t *user_command, char *string);

/**
 * Parses an add command
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if the type argument is missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_add_command(user_command_t *user_command, char **last);

/**
 * Parses a sleep command
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if the seconds argument is missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_sleep_command(user_command_t *user_command, char **last);

/**
 * Parses command target ID
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND` if `"to"` is missing,
 * `INVALID_TARGET_ID` if the target is missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_command_target(user_command_t *user_command, char **last);

/**
 * Parses a switch command
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if the label or position arguments are missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_switch_command(user_command_t *user_command, char **last);

/**
 * Parses a registry set command
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if the label or position arguments are missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_set_command(user_command_t *user_command, char **last);

/**
 * Parses an link command
 * 
 * @param user_command Struct where command data will be put
 * @param last Last parsing position the parsing
 * 
 * @returns `INVALID_COMMAND_ARGUMENT` if the parent argument is missing or invalid,
 * `OK` otherwise
 */
error_code_t parse_link_command(user_command_t *user_command, char **last);

/**
 * Checks the user command for semantic (not syntactic) errors
 * 
 * Also sets `next_hop_fd` then used to send the message
 * 
 * @param user_command The parsed user command
 * 
 * @returns `UNABLE_TO_LOCK_MUTEX` if data mutex could not be locked,
 * `DEVICE_NOT_FOUND` if there is no device with the specified target ID,
 * `DEVICE_TYPE_MISMATCH` if the command is not compatible with the target device,
 * `SYSTEM_OFF` if the system is off and the command is not a switch main,
 * `CANNOT_ADD_TO_PARENT` if the parent wouldn't change or it's a Timer already with a child,
 * `LINKING_PARENT_TO_CHILD` if the link would create cycles,
 * `UNABLE_TO_UNLOCK_MUTEX` if data mutex could not be locked,
 * `OK` otherwise
 */
error_code_t check_user_command(user_command_t *user_command);

/**
 * Executes user command
 * 
 * @param user_command The parsed use command
 * 
 * @returns `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the request,
 * `ROUTE_NOT_FOUND` if the target device could not be found,
 * `UNABLE_TO_WRITE_PIPE` if the write failed,
 * `UNABLE_TO_CREATE_PIPE` if the pipe where the device will receive commands could not be created,
 * `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened in write mode,
 * `UNABLE_TO_LOCK_MUTEX` if the data mutex could not be locked to update routing information,
 * `UNABLE_TO_ALLOCATE_HEAP` if the routing information could not be inserted,
 * `UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked,
 * `OK` otherwise
 */
error_code_t execute_user_command(user_command_t *user_command);

/**
 * Counts the number of directly connected devices, and optionally outputs data about all devices
 * 
 * @param output_data If device data has to be printed
 * 
 * @returns The number of directly connected devices (if positive),
 * `-UNABLE_TO_LOCK_MUTEX` if the mutex could not be locked,
 * `-UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked
 */
int32_t execute_list_command(bool output_data);

/**
 * Created a new device
 * 
 * @param type Type of the new device
 * 
 * @returns `UNABLE_TO_CREATE_PIPE` if the pipe where the device will receive commands could not be created,
 * `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened in write mode,
 * `UNABLE_TO_LOCK_MUTEX` if the data mutex could not be locked to update routing information,
 * `UNABLE_TO_ALLOCATE_HEAP` if the routing information could not be inserted,
 * `UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked,
 * `OK` otherwise
 */
error_code_t execute_add_command(device_type_t type);

/**
 * Sends delete messages to all direct children
 * 
 * @returns `UNABLE_TO_LOCK_MUTEX` if the data mutex could not be locked to get routing information,
 * `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the request,
 * `UNABLE_TO_WRITE_PIPE` if the write failed,
 * `UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked,
 * `OK` otherwise
 */
error_code_t execute_exit_command();

/**
 * Executes scenario, reads `commands.scenario` file and parses each line
 * 
 * @returns `UNABLE_TO_OPEN_FILE` if the scenario file exists and could not be opened,
 * `UNABLE_TO_CLOSE_FILE` if the scenario file could not be closed,
 * `OK` otherwise
 */
error_code_t execute_scenario();

/**
 * Outputs device data
 * 
 * @param device Pointer to the device data
 */
void output_device(routing_data_t *device);

/**
 * Outputs response data
 * 
 * @param response Response to be printed
 */
void output_response(response_t *response);

/**
 * Uses received link response mainly to update routing data
 * 
 * @param response The received response
 * 
 * @returns `CHILD_NOT_FOUND` if the device could not be found,
 * `RESPONSE_FORMAT_ERROR` if the response did not include the new type,
 * `UNABLE_TO_OPEN_PIPE` if a pipe for the new direct child could not be opened,
 * `UNABLE_TO_ALLOCATE_HEAP` if the routing data could not be updated,
 * `UNABLE_TO_CLOSE_PIPE` if the pipe of the old direct child could not be closed,
 * `ROUTE_NOT_FOUND` if the route to the old parent could not be found,
 * `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the request,
 * `UNABLE_TO_WRITE_PIPE` if the write failed,
 * `OK` otherwise
 */
error_code_t update_with_link_response(response_t *response);

/**
 * Uses received delete response mainly to update routing data
 * 
 * @param response The received response
 * 
 * @returns `CHILD_NOT_FOUND` if the device could not be found,
 * `UNABLE_TO_WAIT` it the process could not be waited,
 * `UNABLE_TO_CLOSE_PIPE` if the pipe of the old direct child could not be closed,
 * `UNABLE_TO_REMOVE_PIPE` if the pipe could not be removed,
 * `OK` otherwise
 */
error_code_t update_with_delete_response(response_t *response);

/**
 * Formats user message for received info response
 * 
 * @param response Received response
 * @param user_message String where to put the formatted message, of size `USER_MESSAGE_SIZE`
 * @param type Device type
 */
void format_info_user_message(response_t *response, char user_message[USER_MESSAGE_SIZE], device_type_t type);

/**
 * Formats user message for received registry set response
 * 
 * @param response Received response
 * @param user_message String where to put the formatted message, of size `USER_MESSAGE_SIZE`
 */
void format_set_user_message(response_t *response, char user_message[USER_MESSAGE_SIZE]);

/**
 * Sends command to device using the correct pipe
 * 
 * @param request Request to send
 * @param request_buffer Buffer where to put the formatted request
 * @param size Size of the buffer
 * @param fd File descriptor to use to send the request
 * 
 * @returns `BUFFER_TOO_SHORT` if the formatted request does not fit in the buffer,
 * `REQUEST_FORMAT_ERROR` if there was an error formatting the request,
 * `UNABLE_TO_WRITE_PIPE` if the write failed,
 * `OK` otherwise
 */
error_code_t write_pipe(request_t *request, char *request_buffer, size_t size, int fd);

/**
 * Function that the responses thread executes
 * 
 * It reads from the responses pipe
 * 
 * @param arg Not used
 * 
 * @returns `NULL`
 */
void* responses_routine(void *arg);

/**
 * Creates and opens `"./ipc/<controller_id>_up.fifo"` in read-write mode, this way
 * the open doesn't block and no end of file is returned to the read when the controller is empty
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
 * Closes and removes device fifos
 * 
 * @param device Device to cleanup
 * 
 * @returns `UNABLE_TO_REMOVE_PIPE` if the pipes could not be removed,
 * `OK` otherwise
 */
error_code_t end_child_device_fifos(routing_data_t *device);

/**
 * Handles the shutdown
 * 
 * @param error Error that caused the shutdown, if not `OK`
 * @param in_responses_thread If the function is called from the responses thread, so it should not cancel itself
 */
void handle_shutdown(error_code_t error, bool in_responses_thread);

/**
 * Sends `SIGTERM` to all devices that have the specified parent ID and their children, and cleans up their pipes
 * 
 * @param parent_id The parent ID
 * @param complete If also children should be removed from the routing table and their pipes should be cleaned up
 * 
 * @returns `UNABLE_TO_SEND_SIGNAL` if the `SIGTERM` signal could not be sent,
 * `UNABLE_TO_REMOVE_PIPE` if a pipe could not be removed and not because the file didn't exist,
 * `OK` otherwise
 */
error_code_t shutdown_devices(device_id_t parent_id, bool complete);

/**
 * Handles the shutdown caused by a `SIGTERM` or `SIGINT` signal
 * 
 * @param sig_num Signal number, unused
 */
void sigterm_handler(int sig_num);

/**
 * Handles the resizing of the terminal notified by a `SIGWINCH` signal
 * 
 * @param sig_num Signal number, unused
 */
void sigwinch_handler(int sig_num);


/**
 * Handles the shutdown of a child process, logical direct or indirect child device
 * 
 * It creates a detached thread that locks the mutex to safely access data
 * 
 * @param sig_num Signal number, unused
 */
void sigchld_handler(int sig_num);

/**
 * Handles the shutdown of a child process, logical direct or indirect child device
 * 
 * It locks the mutex to safely access data
 */
void* sigchld_routine(void *arg);

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
 * so this function doesn't check for all errors
 * 
 * @param arg Not used
 * 
 * @returns `NULL`
 */
void* stderr_routine(void *arg);

/**
 * Prints to `stdout` or `ncurses` based on current mode
 * 
 * Do not include a new line, as it's position changes based on the current mode,
 * so it's added automatically
 * 
 * It doesn't check for errors because nothing can be done
 * 
 * @param format Format string
 * @param ... Additional arguments
 * 
 * @returns The number of written chars
 */
int output(char *format, ...);

/**
 * Reads from `stdin` or `ncurses` based on current mode
 * 
 * It doesn't check for errors because nothing can be done
 * 
 * @param buffer Where the input will be written
 * @param size Buffer size
 * 
 * @returns The number of chars read (if positive),
 * `EOF` if the stream has been closed,
 * `-UNABLE_TO_READ_FILE` if there was an error while reading,
 * `-BUFFER_TOO_SHORT` if the buffer was too short
 */
ssize_t input(char *buffer, size_t size);

/**
 * Ends `ncurses` restoring the terminal to normal mode
 */
error_code_t end_ncurses();

#endif
