/**
 * This file contains various utility function declarations
 */

#ifndef DOMOTICS_UTILS_H
#define DOMOTICS_UTILS_H

#include "devices.h"
#include "return_codes.h"

#define _XOPEN_SOURCE 700
#include <stdbool.h>
#include <sys/types.h>

#define PIPE_NAME_MAX_LENGTH 24
#define PIPE_PERMISSIONS 0660
#define NO_FILE_DESCRIPTOR -1

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
 * @param 
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
 * Create the pipe name from the ID and the direction
 * 
 * @param device_id The device ID
 * @param down `true` if the direction is down, `false` if the direction is up
 * @param buffer Where to put the name string
 * @param size Maximum size of the buffer
 * 
 * @returns `BUFFER_TOO_SHORT` if the buffer is too short,
 * `CODE_FORMAT_ERROR` if there was a problem formatting the string,
 * `OK` otherwise
 */
error_code_t create_fifo_name(device_id_t device_id, bool down, char* buffer, size_t size);

/**
 * Initializes device fifos, opens the pipe to the Controller and creates its pipe in `"./ipc/<id>_down.fifo"`
 * and optionally `"./ipc/<id>_up.fifo"` for a control device
 * 
 * @param device_id The device ID
 * @param rcv_commands_fd Pointer where the function will put the file descriptor where to receive commands from the parent
 * @param snd_responses_fd Pointer where the function will put the file descriptor where to send responses to the parent
 * @param rcv_responses_fd Pointer where the function will put the file descriptor where to receive responses from the
 * child/children, `NULL` for a leaf device
 * 
 * This function blocks until pipes opened in reading mode are opened in writing mode by other processes and viceversa,
 * for testing use `echo "" > ./ipc/<id>_down.fifo` and `cat 0_up.fifo`
 * 
 * If any error happens the function exits as it's a critical non-solvable error
 * 
 * It generates an error if a pipe to be created already exists, the pipe could have something written in it
 * if it already exists, and should not be opened
 * 
 * The pipe should be deleted by the device when it exists or by the Controller if it doesn't respond,
 * this is not handled here and can be fixed with a `make clean`
 */
void start_device_fifos(device_id_t device_id, int *rcv_commands_fd, int *snd_responses_fd, int *rcv_responses_fd);

/**
 * Closes and deletes devices fifos
 * 
 * @param device_id The device ID
 * @param rcv_commands_fd File descriptor of the pipe where to receive commands from the parent
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
error_code_t end_device_fifos(device_id_t device_id, int rcv_commands_fd, int snd_responses_fd, int rcv_responses_fd);

#endif
