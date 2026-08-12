/**
 * This file contains type definitions, constant definitions and function declarations specific to the Hub
 */

#ifndef DOMOTICS_HUB_H
#define DOMOTICS_HUB_H

#include "utils.h"
#include "return_codes.h"
#include "messages.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <routing.h>
#include <fcntl.h>

#define ADDITIONAL_INFO_ARGUMENT 2
#define ADDITIONAL_SWITCH_ARGUMENT 0
#define ADDITIONAL_DELETE_ARGUMENT 0

typedef struct pending_t {
    command_code_t command_code;
    device_id_t *pending_devices;
    size_t pending_devices_size;
    control_device_state_t state;
    u_int16_t max_time;
    bool has_error;
    bool is_complete;
    struct pending_t *next;
} pending_t;

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
 * It manages the communication between the hub and the parent, receives the requests from the parent and write the responses back
 * 
 * Eventually it can forward the request to its children
 * 
 * @return An error code to do the exit
 */
error_code_t top_down_handler();

/**
 * It tries to read the pipe
 * 
 * @param fd The file descriptor linked to the pipe
 * @param buffer The buffer in which the string read is put
 * @param buffer_size The size of the buffer
 * @return
 *  - `OK` If no errors occurred
 * 
 *  - `UNEXPECTED_END_OF_FILE` If it was an end of file (EOF)
 * 
 *  - `UNABLE_TO_READ_PIPE` If errors occurred
 */
error_code_t read_pipe(int fd, char *buffer, size_t buffer_size);

/**
 * It creates and initialize with default values the pending requests list, it can fail
 * 
 * @param command_code the command_code of the pending request
 * @return the initialized pending requests list
 */
pending_t* init_pending(command_code_t command_code);

/**
 * It formats the request and writes the string created in the pipe managing the errors that can occur
 * 
 * @param request The request to send
 * @param buffer_write The buffer in which to write the string-formatted request
 * @param snd_request_fd The file descriptor that identifies the pipe
 */
void write_pipe_request(request_t* request, char* buffer_write, int snd_request_fd);

/**
 * Closes a pipe used to reach a direct child that has being moved 
 *
 * @param child_id the id of the child
 * @returns
 *  - `OK` if no errors occurred
 * 
 *  - `CHILD_NOT_FOUND` if it has no children or if it's not a direct child
 * 
 * - `UNABLE_TO_CLOSE_PIPE` if the pipe couldn't be close
 */ 
error_code_t link_remove_child(device_id_t child_id);

/**
 * Finds the routing data information of a direct child given the child id
 * 
 * @param child_id The device id of the child
 * @return The routing data information found, can be NULL if the searched child has not be found
 */
routing_data_t* find_direct_child(device_id_t child_id);

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

/**
 * It takes the new parent id from the request, if it's different the pipe to talk to the parent is changed
 * and also the current parent id then a partial response is created by adding the parent id and the device type
 * 
 * @param new_parent_id the new parent id received from the request
 * @param parent_changed to set as true if the parent has changed to perform a replay history
 * @return the error code to send in the response
 */
error_code_t link_change_parent(device_id_t new_parent_id, bool *parent_changed);

/**
 * It search if the child exists between its children, if not, an error response is created, otherwise it returns a file descriptor 
 * to which send the request
 * 
 * @param request The request to forward
 * @return The file descriptor to which send the request or `-1` if no child was found
 */
void send_to_child(request_t *request);

/**
 * It formats the response and writes the string created in the pipe to send a response to the parent
 * managing the errors that can occur
 * 
 * @param response The response to send
 * @param buffer_write The buffer in which to write the string-formatted response
 */
void write_pipe_response(response_t* response, char* buffer_write);

/**
 * A response to the new parent is sent for every direct and indirect children that the hub has
 * 
 * The response includes the parent id of the child, its device type and the source is as the child id
 * as if it was done by the child itself
 * 
 * @param response the response to send back
 */
void replay_history(response_t *response);

/**
 * It tries to find the child in the routing table, if it succeeds the routing information is removed,
 * if it fails an error code is returned
 * 
 * @param child_id the child id to find its routing information
 * @returns
 *  - `OK` if it succeeds
 * 
 *  - `ROUTE_NOT_FOUND` if it fails
 */
error_code_t link_remove_child_received(device_id_t child_id);

/**
 * It adds the routing information of the new child, if it's a direct child it also open a pipe
 * 
 * @param response the received response, will be modified
 */
void add_child(response_t *response);

/**
 *  If a LINK_REMOVE_CHILD response arrives the routing table is changed otherwise it's a LINK_CHANGE_PARENT response
 *  and new routing information need to be added to the routing table if it's a direct child a pipe is opened
 * 
 * @param response the received response, it will be modified
 */
void link_response(response_t *response);

/**
 * The pending response has be resolved and the arguments of the info response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void info_response(response_t *response, pending_t *solved_response);

/**
 *  The pending response has be resolved and the arguments of the switch response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void switch_response(response_t *response, pending_t *solved_response);

/**
 *  The pending response has be resolved and the arguments of the delete response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void delete_response(response_t *response, pending_t *solved_response);

/**
 * It receives the responses from the children and if the response given is not in the pending ones it will be forwarded upwards, otherwise
 * it will be checked as arrived (`NO_ID`) and if all the children has given their response a cumulative response is done and sent upwards as the hub response
 * 
 * @param arg Not used
 * 
 * @returns `NULL`
 * 
 */
void* bottom_up_handler(void* arg);

/**
 * Handles the shutdown caused by a `SIGTERM` signal
 * @param sig_num Signal number, unused
 */
void sigterm_handler(int sig_num);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 * 
 * @param error The error to handle
 */
void handle_shutdown(error_code_t error);

//TODO
void pending_update(pending_t *pending, response_t *response);

void format_response_type(response_t *response, pending_t *pending);

pending_t* check_pending(response_t* response, pending_t **previous);

pending_t* check_pending_2(response_t* response, pending_t **previous);

void check_complete_and_send(response_t *response);

void free_pending(pending_t **pending, pending_t *previous);

error_code_t forward_to_children(request_t *request);

#endif
