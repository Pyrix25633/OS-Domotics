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
#define ADDITIONAL_DELETE_ARGUMENT 0

// - Macros -

#define END_ALL_FIFOS end_device_fifos(id, rcv_requests_parent_fd, snd_responses_parent_fd, rcv_responses_children_fd)
#define END_CHILDREN_FIFO end_device_fifos(id, NO_FILE_DESCRIPTOR, NO_FILE_DESCRIPTOR, rcv_responses_children_fd)

typedef struct linked_list_t {
    command_code_t command_code;
    device_id_t *pending_devices;
    size_t pending_devices_size;
    control_device_state_t state;
    u_int16_t max_time;
    struct linked_list_t *next;
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
error_code_t read_pipe(int fd, char* buffer, size_t buffer_size);

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
 * It checks the type of link request and performs the correct operation
 * 
 * @param request the request received from the parent
 * @param response the response to send back
 * @param parent_changed to set as true if the parent has changed to perform a replay history
 */
void create_link(request_t *request, response_t *response, bool *parent_changed);


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
 * @param request the request received from the parent
 * @param response the response to send back
 * @param parent_changed to set as true if the parent has changed to perform a replay history
 * @return the error code to send in the response
 */
error_code_t link_change_parent(request_t *request, response_t *response, bool *parent_changed);

/**
 * It search if the child exists between its children, if not, an error response is created, otherwise it returns a file descriptor 
 * to which send the request
 * 
 * @param response The response to send back
 * @param destination The id of the child to search
 * @return The file descriptor to which send the request or `-1` if no child was found
 */
int send_to_child(response_t *response, device_id_t destination);

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
 * @param response_buffer the buffer to write the responses
 */
void replay_history(response_t *response, char *response_buffer);

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
 * Checks if a response is still missing to complete a pending response, if it does the pending response is updated
 * 
 * If it's a delete response the routing table is also updated
 * 
 * If the response is from a control device the response code can be OK while the additional argument can provide an error
 * so this is also checked
 * 
 * @param response the received response, it will be modified
 * @param found it will be set as `true` if it was founded in the missing ones
 * @param is_complete it will be set as `true` if the pending response is now complete and can be sent
 * @param found_request it will be initialized with the pending response if it was missing
 */
void check_pending_complete(response_t *response, bool *found, bool *is_complete, linked_list_t *found_request);

/**
 * The pending response has be resolved and the arguments of the info response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void info_response(response_t *response, linked_list_t *solved_response);

/**
 *  The pending response has be resolved and the arguments of the switch response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void switch_response(response_t *response, linked_list_t *solved_response);

/**
 *  The pending response has be resolved and the arguments of the delete response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void delete_response(response_t *response, linked_list_t *solved_response);

/**
 * It frees the space before allocated for the pending responses struct
 */
void free_pending_response();

/**
 * It receives the responses from the children and if the response given is not in the pending ones it will be forwarded upwards, otherwise
 * it will be checked as arrived (`NO_ID`) and if all the children has given their response a cumulative response is done and sent upwards as the hub response
 * 
 */
void bottom_up_handler();

/**
 * Handles the shutdown caused by a `SIGTERM` signal
 */
void sigterm_handler();

/**
 * Handles the shutdown caused by a `SIGPIPE` signal
 */
void sigpipe_handler();

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 * 
 * @param error The error to handle
 */
void handle_shutdown(error_code_t error);

#endif
