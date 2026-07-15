#define _XOPEN_SOURCE 700

#include "fridge.h"
#include "utils.h"
#include "return_codes.h"
#include "messages.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

// - Explicit device data -

device_id_t id; // Never changes
leaf_device_state_t state = STATE_CLOSED; // Modified by switch commands
u_int8_t autoclose_delay = DEFAULT_AUTOCLOSE_DELAY; // Can be modified
u_int8_t fill_percentage = DEFAULT_FILL_PERCENTAGE; // Can be modified only manually
u_int8_t thermostat = DEFAULT_THERMOSTAT; // Can be modified only manually
u_int32_t seconds_open = INITIAL_SECONDS_OPEN; // Updated automatically, converted into hours for info request
u_int8_t last_temperature = INITIAL_TEMPERATURE; // Updated automatically

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
time_t last_opened; // Timestamp needed to calculate the open time and current temperature
temperature_direction_t temperature_direction = INITIAL_TEMPERATURE_DIRECTION; // Temperature direction needed to calculate current temperature

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely

// - IPC data -

int rcv_requests_fd;
int snd_responses_fd;
bool force_exit = false;

// - Signal handler variables -

struct sigaction action_handler;

// TODO: implement autoclose
// TODO: organize code consistently

int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv);

    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);

    // TODO: check what happens when the parent closes the pipe because the device is moved around
    // TODO: it probably exits from the while, it must be solved somehow
    char request_buffer[MAX_REQUEST_SIZE];
    char response_buffer[MAX_RESPONSE_SIZE];
    request_t request;
    response_t response;
    response.source = id;
    error_code_t error_code;
    while(!force_exit) {
        response.arguments_size = 0;
        if(read(rcv_requests_fd, request_buffer, MAX_REQUEST_SIZE) < 0 || printf("Read: %s\n", request_buffer) < 0) { // ! remove print
            response.command_code = NULL_COMMAND; // Could not be parsed
            response.response_code = UNABLE_TO_READ_PIPE;
        }
        else if((error_code = parse_request(&request, request_buffer, MAX_REQUEST_SIZE)) != OK) {
            response.command_code = NULL_COMMAND; // Could not be parsed
            response.response_code = error_code;
        }
        else {
            response.command_code = request.command_code;
            response.response_code = OK;
            response.arguments_size = 0;
            if(pthread_mutex_lock(&data_mutex) < 0) {
                response.response_code = UNABLE_TO_LOCK_MUTEX;
            } else {
                if(request.destination != id) {
                    response.response_code = DESTINATION_ID_MISMATCH;
                } else if(IS_INFO(request.command_code)) {
                    response.arguments_size = 6;
                    response.arguments[STATE_ARGUMENT] = state;
                    response.arguments[OPEN_HOURS_ARGUMENT] = calculate_seconds_open() / (60*60);
                    response.arguments[AUTOCLOSE_DELAY_ARGUMENT] = autoclose_delay;
                    response.arguments[FILL_PERCENTAGE_ARGUMENT] = fill_percentage;
                    response.arguments[THERMOSTAT_ARGUMENT] = thermostat;
                    response.arguments[TEMPERATURE_ARGUMENT] = calculate_current_temperature();
                } else if(IS_LINK(request.command_code) && LINK_SUBCOMMAND(request.command_code) == LINK_CHANGE_PARENT) {
                    if(request.argument != parent_id) {
                        parent_id = request.argument;
                        response.response_code = change_snd_responses_pipe(request.argument, &snd_responses_fd);
                    }
                } else if(IS_DELETE(request.command_code)) {
                    // TODO: check if there are other things to do
                    force_exit = true;
                } else if(IS_REGISTRY(request.command_code)) {
                    if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_DELAY) {
                        autoclose_delay = request.argument;
                    } else if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_THERMOSTAT) {
                        // TODO: validate thermostat
                        thermostat = request.argument;
                    } else if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_PERCENTAGE) {
                        if(request.argument <= 100) {
                            fill_percentage = request.argument;
                        } else {
                            response.response_code = INVALID_REQUEST_ARGUMENT;
                        }
                    }
                } else if(IS_SWITCH(request.command_code)) {
                    if(SWITCH_LABEL(request.command_code) == SWITCH_OPEN) {
                        if(SWITCH_POSITION(request.command_code) == POSITION_ON) {
                            set_state(STATE_OPEN);
                        }
                    } else if(SWITCH_LABEL(request.command_code) == SWITCH_CLOSE) {
                        if(SWITCH_POSITION(request.command_code) == POSITION_ON) {
                            set_state(STATE_CLOSED);
                        }
                    } else {
                        response.response_code = INVALID_COMMAND;
                    }
                } else {
                    response.response_code = UNEXPECTED_COMMAND;
                }
                if(pthread_mutex_unlock(&data_mutex) < 0) {
                    print_error(STDERR_FILENO, UNABLE_TO_UNLOCK_MUTEX, id, "while processing request");
                }
            }
        }
        // TODO: wait a random time
        error_code = format_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        if(error_code != OK) {
            print_error(STDERR_FILENO, error_code, id, "while formatting response");
        } else if(write(snd_responses_fd, response_buffer, MAX_RESPONSE_SIZE) < 0) {
            print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
        }
    }

    handle_shutdown();
}

void handle_shutdown() {
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, NO_FILE_DESCRIPTOR);
    if(error_code != OK) {
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    // TODO: add here things to do on deletion
    exit(error_code);
}

void set_state(leaf_device_state_t new_state) {
    if(new_state != state) {
        state = new_state;
        if(state == STATE_OPEN) {
            last_opened = time(NULL);
        } else {
            seconds_open = calculate_seconds_open();
        }
        last_temperature = calculate_current_temperature();
    }
}

u_int32_t calculate_seconds_open() {
    if(state == STATE_OPEN) {
        return seconds_open + (time(NULL) - last_opened);
    }
    return seconds_open;
}

u_int8_t calculate_current_temperature() {
    // TODO: actually implement the function
    return last_temperature;
}