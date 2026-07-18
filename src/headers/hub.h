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

#define UNDEFINED_STATE 0x10;

// - Macros -

#define END_ALL_FIFOS end_device_fifos(id, rcv_requests_parent_fd, snd_responses_parent_fd, rcv_responses_children_fd)
#define END_CHILDREN_FIFO end_device_fifos(id, NO_FILE_DESCRIPTOR, NO_FILE_DESCRIPTOR, rcv_responses_children_fd)

typedef struct linked_list_t {
    command_code_t command_code;
    device_id_t *responded;
    size_t responded_size;
    linked_list_t *next;
    leaf_device_state_t state;
    u_int16_t max_time;

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
 * Closes the fifos while checking for errors
 * @param is_children_EOF if true closes only the rcv_requests_children_fd fifo
 */
error_code_t close_fifos(bool is_children_EOF);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 */
void handle_shutdown();



#endif
