/**
 * This file contains various utility function declarations
 */

#ifndef DOMOTICS_UTILS_H
#define DOMOTICS_UTILS_H

#include "devices.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"

#include <stdbool.h>
#include <sys/types.h>
#include <string.h>

#define PIPE_NAME_MAX_LENGTH       24
#define EXECUTABLE_NAME_MAX_LENGTH 16
#define PIPE_PERMISSIONS           0660
#define NO_FILE_DESCRIPTOR         -1
#define TIME_SIZE                  8

// Pipes

typedef bool pipe_direction_t;

#define DIRECTION_UP   0
#define DIRECTION_DOWN 1

// Processing time

#define MIN_PROCESSING_TIME 1
#define MAX_PROCESSING_TIME 3

// User commands

typedef u_int8_t user_command_code_t;

#define MESSAGE_FLAG   0b1000
#define ADD_COMMAND    0b0000
#define DELETE_COMMAND 0b1000
#define SWITCH_COMMAND 0b1001
#define LINK_COMMAND   0b1010
#define SET_COMMAND    0b1011
#define INFO_COMMAND   0b1100
#define LIST_COMMAND   0b0001

#define IS_MESSAGE(c) ((c & MESSAGE_FLAG) == MESSAGE_FLAG)

typedef struct user_command_t {
    device_id_t target;
    user_command_code_t code;
    command_code_t message_code;
    u_int16_t argument;
} user_command_t;

/**
 * Measures the length of a possibly not NULL-terminated string
 * @param string The string to measure
 * @param max_length The maximum length of the string
 * @returns The length of the string, max_length if the string is not NULL-terminated
 */
size_t string_length(char *string, size_t max_length);

/**
 * Converts a string into an unsigned, checking for errors
 * @param string NULL-terminated string to be converted
 *
 * @returns The converted number (positive),
 * `-CODE_FORMAT_ERROR` if the number is not correctly formatted
 */
int string_to_unsigned(char *string);

/**
 * Parses the arguments to get the device ID, which should be the first
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * 
 * @returns The ID if found and valid, otherwise prints the error and exits
 */
int get_id_from_arguments(int argc, char *argv[]);

/**
 * Sets the signal handler for a specific signal, with flag `SA_RESTART`, which means that any
 * system call that was blocking before the signal arrived is restarted instead of failing
 * 
 * @param signal Signal code
 * @param signal_handled Handler function
 * 
 * If the operation fails, the function exits with code `UNABLE_TO_SET_SIGHANDLER`
 */
void set_signal_handler(int signal, void (*signal_handler)());

/**
 * Create the pipe name from the ID and the direction
 * 
 * @param device_id The device ID
 * @param direction The pipe direction
 * @param buffer Where to put the name string
 * @param size Maximum size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the buffer is too short,
 * `CODE_FORMAT_ERROR` if there was a problem formatting the string,
 * `OK` otherwise
 */
error_code_t create_fifo_name(device_id_t device_id, pipe_direction_t direction, char* buffer, size_t size);

/**
 * Initializes device fifos, opens the pipes to the Controller and its pipe in `"./ipc/<id>_down.fifo"`
 * and optionally creates and opens `"./ipc/<id>_up.fifo"` for a control device
 * 
 * @param device_id The device ID
 * @param rcv_requests_fd Pointer where the function will put the file descriptor where to receive requests from the parent
 * @param snd_responses_fd Pointer where the function will put the file descriptor where to send responses to the parent
 * @param rcv_responses_fd Pointer where the function will put the file descriptor where to receive responses from the
 * child/children, `NULL` for a leaf device, opened in read-write to avoid end of file when there are no more children in write
 * 
 * If any error happens the function exits as it's a critical non-solvable error
 * 
 * It generates an error if a pipe to be created already exists, the pipe could have something written in it
 * if it already exists, and should not be opened
 * 
 * The pipe should be deleted by the device when it exists or by the Controller if it doesn't respond,
 * this is not handled here and can be fixed with a `make clean`
 */
void start_device_fifos(device_id_t device_id, int *rcv_requests_fd, int *snd_responses_fd, int *rcv_responses_fd);

/**
 * Closes and deletes devices fifos
 * 
 * @param device_id The device ID
 * @param rcv_requests_fd File descriptor of the pipe where to receive requests from the parent
 * @param snd_responses_fd File descriptor of the pipe where to send responses to the parent
 * @param rcv_responses_fd File descriptor of the pipe where to receive responses from the child/children,
 * `NO_FILE_DESCRIPTOR` for a leaf device
 * 
 * If an error occurs, the function goes on trying to close and delete the remaining fifos,
 * it then returns the last error code if any
 * 
 * @returns`UNABLE_TO_CLOSE_PIPE` if there was an error closing the pipe,
 * `UNABLE_TO_REMOVE_PIPE` if there was an error removing the pipe,
 * `OK` otherwise
 */
error_code_t end_device_fifos(device_id_t device_id, int rcv_requests_fd, int snd_responses_fd, int rcv_responses_fd);

/**
 * Closes the current pipe used to send requests to the parent and replaces it with the new one
 * 
 * If it fails to open the new pipe, the old one is left open
 * 
 * @param parent_id New parent device ID
 * @param snd_responses_fd Pointer where to get the old file descriptor and put the new file descriptor
 * 
 * @returns `UNABLE_TO_CLOSE_PIPE` if the pipe could not be closed,
 * `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened,
 * `OK` otherwise
 */
error_code_t change_snd_responses_pipe(device_id_t parent_id, int *snd_responses_fd);

/**
 * Functions that sleeps a random amount of time to simulate processing time
 */
void simulate_processing_time();

/**
 * Parses string in `"<hours>:<minutes>"` to number of minutes
 * 
 * The passed string is modified and will no longer be valid
 * 
 * @param time Formatted string to parse
 * 
 * @returns The converted time (positive),
 * `-CODE_FORMAT_ERROR` if the time is not correctly formatted
 */
int parse_time(char *time);

/**
 * Formats string in `"<hours>:<minutes>"` from number of minutes
 * 
 * @param time String where to put the formatted time, of size `TIME_SIZE`
 * @param minutes Number of minutes
 */
void format_time(char time[TIME_SIZE], u_int16_t minutes);

/**
 * Creates the executable name for `exec`
 * 
 * @param type The device type
 * @param path String where to put the full relative path, of size `EXECUTABLE_NAME_MAX_LENGTH`
 * @param name String where to put the executable name, of size `EXECUTABLE_NAME_MAX_LENGTH`
 */
void create_executable_name(device_type_t type, char path[EXECUTABLE_NAME_MAX_LENGTH], char name[EXECUTABLE_NAME_MAX_LENGTH]);

#endif
