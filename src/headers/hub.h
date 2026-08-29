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
#define NO_ROUTE -1

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
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_SET_SIGHANDLER` if a signal handler could not be set,
 * `UNABLE_TO_CREATE_PIPE` if the pipe where the device will receive commands could not be created,
 * `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened in write mode,
 * `UNABLE_TO_CLOSE_PIPE` if the pipe of the old direct child could not be closed,
 * `UNABLE_TO_REMOVE_PIPE` if the pipe could not be removed,
 * `UNABLE_TO_CREATE_THREAD` if the thread dedicated to reading responses could not be created,
 * `UNEXPECTED_END_OF_FILE` if the pipe was closed by the parent,
 * `UNABLE_TO_READ_PIPE` if there was an error reading the pipe,
 * `BUFFER_TOO_SHORT` if the string is not NULL-terminated and so the reading buffer was too short,
 * `REQUEST_FORMAT_ERROR` if there was an error in the format,
 * `UNABLE_TO_LOCK_MUTEX` if data mutex could not be locked,
 * `UNABLE_TO_UNLOCK_MUTEX` if the mutex could not be unlocked,
 * `UNEXPECTED_SHUTDOWN` if it received `SIGTERM` or `SIGINT`,
 * `UNEXPECTED_COMMAND` if the command received cannot be handled,
 * `UNABLE_TO_ALLOCATE_HEAP` if the malloc has failed,
 * `CHILD_NOT_FOUND` if the device could not be found,
 * `ROUTE_NOT_FOUND` if the target device has been removed from the routing table before the request could be sent,
 * `UNABLE_TO_CANCEL_THREAD` if the bottom_up_handler thread could not be cancelled,
 * `UNABLE_TO_JOIN_THREAD` if the bottom_up_handler thread could not be joined,
 * `OK` otherwise
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
 * It creates and initialize with default values a pending entry, it can fail due to malloc
 * 
 * @param command_code the command_code of the request
 * @return the initialized pending entry
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
 * It tries to remove the routing information of a child from the routing table,
 * the routing table is updated and also the device type
 *
 * @param response to be set in case of errors
 * @param parent_id the id of the parent, set it to NO_ID if the child data is needed
 * @return `true` if no errors occurred, `false` otherwise
 */ 
bool remove_child(response_t *response, device_id_t parent_id);

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
 * It takes the new parent id from the request, if it's different the pipe to communicate with the parent is changed
 * and also the current parent id
 * 
 * @param request the arrived request
 * @param response to be set in case of errors
 * @return `true` if no errors occurred, `false` otherwise
 */
bool link_change_parent(request_t *request, response_t *response);

/**
 * It forwards the request to a child
 * 
 * @param request The request to forward
 * @param response to be set in case of errors
 * @return `true` if a response must be sent, `false` otherwise
 */
bool send_to_child(request_t *request, response_t *response);

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
 * The response includes the parent id of the child, its device type and the source is the child id
 * as if it was done by the child itself
 * 
 * @param response the response to send back
 */
void replay_history(response_t *response);

/**
 * It reads the child id from the response and it tries to find the child in the routing table, 
 * if it succeeds it tries to remove the routing information from the routing table, 
 * the routing table is updated and also the device type 
 * 
 * @param response the received responses
 * @returns
 *  - `OK` if it succeeds
 * 
 *  - `ROUTE_NOT_FOUND` if it fails
 */
error_code_t link_remove_child_received(response_t *response);

/**
 * If the child to add was already one of its child it tries to close it's pipe, 
 * then it adds the routing information of the new child to the routing table, 
 * if it's a direct child it also open a pipe
 * 
 * The device type is updated
 * 
 * @param response the received response, will be modified
 */
void add_child(response_t *response);

/**
 *  It manages what to do based on the link response type received
 * 
 * @param response the received response, it will be modified
 */
void link_response_bottom_up(response_t *response);

/**
 * The pending response is resolved and the arguments of the info response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void info_response(response_t *response, pending_t *solved_response);

/**
 *  The pending response is resolved and the arguments of the switch response are set
 * 
 * @param response The response to modify in order to be sent later
 * @param solved_response The solved pending response
 */
void switch_response(response_t *response, pending_t *solved_response);

/**
 *  The pending response is resolved and the arguments of the delete response are set
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

/**
 * Opens a pipe to send request to a child
 * 
 * @param device_id the id of the child
 * @param snd_requests_child the file descriptor in which the number is put after the open
 * @return
 *  - `OK` if it succeeds
 * 
 *  - `UNABLE_TO_OPEN_PIPE` if it fails
 */
error_code_t open_pipe(device_id_t device_id, int *snd_requests_child);

/**
 * The current pending is updated based on the received response
 * 
 * @param pending the pending to update
 * @param response the received response
 */
void update_pending(pending_t *pending, response_t *response);

/**
 * It formats the response with the pending data based on the response type
 * 
 * @param response the response to format
 * @param pending the pending to take the data from
 */
void format_response_type(response_t *response, pending_t *pending);

/**
 * Checks if a response can match a pending device response in the pending list
 * 
 * @param response to take the command code and the source
 * @param previous in which to put the previous pending in the list to manage a pending deletion correctly
 * @param ignore_command `TRUE` if the command code is not to be considered `FALSE` otherwise
 * @return the pending that has the device id in the pending device
 */
pending_t* check_pending(response_t* response, pending_t **previous, bool ignore_command);

/**
 * Checks if a device that is being deleted can match a pending device response in the pending list
 * if it does every pending in the list sets the device response as arrived
 * 
 * @param response the response to be sent
 */
void check_complete_and_send(response_t *response);

/**
 * Deletes a pending in the pending list
 * 
 * @param pending the pending to be removed from the list
 * @param previous the previous pending to manage the deletion correctly
 */
void free_pending(pending_t **pending, pending_t *previous);

/**
 * Forwards a request to the children to get their information about it, in order to send
 * an aggregate response later the response to send is put in the pending list
 * 
 * @param request the request to be sent
 * @param response to be set in case of errors
 * @return `true` if no errors occurred, `false` otherwise
 */
bool forward_to_children(request_t *request, response_t *response);

/**
 * It updates the device type, the `child_id` given is ignored in the search
 * (used with link and delete response/request)
 * 
 * It searches between all direct children if at least one is not empty, 
 * if all are empty the device type is updated otherwise it isn't
 * 
 * @param child_id the child id of the one to ignore
 */
void update_type(device_id_t child_id);

/**
 * It reads the pipe and parses the request
 * 
 * @param request where to put the data
 * @param response to be set in case of errors
 * @return `true` if no errors occurred, `false` otherwise
 */
bool get_request(request_t *request, response_t *response);

/**
 * It performs the link commands requested
 * 
 * @param request where to get the data from
 * @param response to be set in case of errors
 */
void link_response_top_down(request_t *request, response_t *response);

/**
 * It performs the requested command
 * 
 * @param request where to get the data from
 * @param response where to put the data in
 * @return `true` if a response must be sent, `false` otherwise
 */
bool execute_command_top_down(request_t *request, response_t *response);

#endif
