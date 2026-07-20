/**
 * This file contains type definitions, constant definitions and function declarations specific to the Hub
 */

#ifndef DOMOTICS_HUB_H
#define DOMOTICS_HUB_H

#include "utils.h"
#include "return_codes.h"
#include "messages.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <routing.h>
#include <fcntl.h>

#define ADDITIONAL_INFO_ARGUMENT 2
#define ADDITIONAL_SWITCH_ARGUMENT 0
#define ADDITIONAL_DELETE_ARGUMENT 0 //TODO check

#define CHILD_ERROR 0x11

// - Macros -

#define END_ALL_FIFOS end_device_fifos(id, rcv_requests_parent_fd, snd_responses_parent_fd, rcv_responses_children_fd)
#define END_CHILDREN_FIFO end_device_fifos(id, NO_FILE_DESCRIPTOR, NO_FILE_DESCRIPTOR, rcv_responses_children_fd)

typedef struct linked_list_t {
    command_code_t command_code;
    device_id_t *requested;
    size_t requested_size;
    leaf_device_state_t state;
    u_int16_t max_time;
    linked_list_t *next;
    bool has_error;
} linked_list_t;

/**
 * Main function of the Hub Program
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
 * A new pending request is created (linked_list_t), then for every direct child it forwards the request and inserts its id in a list to know which
 * responses are arrived and which are not
 * 
 * @param request the request received that has to be modified to be forwarded
 * @param is_forward_request it will be set as true in order to not send a request or a response yet
 * @param buffer_write used to write the response to forward
 */
void forward_request(request_t *request, bool *is_forward_request, char* buffer_write);

/**
 * It creates and initialize with default values the pending requests list
 * 
 * @param command_code the command_code of the pending request
 * @return the initialized pending requests list
 */
linked_list_t* init_pending_requests(command_code_t command_code);

/**
 * It formats the request and writes the string created in the pipe managing the errors that can occur
 * 
 * @param request The request to send
 * @param buffer_write The buffer in which to write the string-formatted request
 * @param snd_request_fd The file descriptor that identifies the pipe
 */
void write_pipe_request(request_t* request, char* buffer_write, int snd_request_fd);

/**
 * Adds a new request to the pending requests list, at the beginning if the list is empty or at the end otherwise
 * 
 * @param pending The list of pending requests
 */
void add_request(linked_list_t *pending);

/**
 * Finds the routing data information of a direct child given the child id
 * 
 * @param child_id The device id of the child
 * @return The routing data information found, can be NULL if the searched child has not be found
 */
routing_data_t* find_direct_child(device_id_t child_id);

/**
 * It manage what to do when a switch request occur, it can be forwarded if the device type 
 * it's correct otherwise it creates an error response to send back
 * 
 * @param request the request received from the parent
 * @param response the response to create to send send it back
 * @param to_be_forwarded true if no response or request need to be send immediately, when the request is forwarded
 * @param buffer_write the buffer to write the request for the children
 */ 
void create_switch(request_t *request, response_t *response, bool* to_be_forwarded, char* buffer_write);

/**
 * Closes a pipe
 * 
 * @param fd the file descriptor of the pipe
 * @return
 *  - `OK` if no errors occurred
 * 
 *  - `UNABLE_TO_CLOSE_PIPE` if the pipe cannot be closed due to errors
 */
error_code_t close_pipe(int fd);

#endif
