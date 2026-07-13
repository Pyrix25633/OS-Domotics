#include "fridge.h"
#include "utils.h"
#include "return_codes.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

// - Explicit device data -

device_id_t id; // Never changes
leaf_device_state_t state = STATE_CLOSED; // Modified by switch commands
u_int8_t autoclose_delay = DEFAULT_AUTOCLOSE_DELAY; // Can be modified
u_int8_t fill_percentage = DEFAULT_FILL_PERCENTAGE; // Can be modified only manually
u_int8_t thermostat = DEFAULT_THERMOSTAT; // Can be modified only manually
u_int32_t seconds_open = INITIAL_SECONDS_OPEN; // Updated automatically, converted into hours for info command
u_int8_t current_temperature = INITIAL_TEMPERATURE; // Updated automatically

// - Auxiliary device data -

time_t last_opened; // Timestamp needed to calculate the open time and current temperature
temperature_direction_t temperature_direction = INITIAL_TEMPERATURE_DIRECTION; // Temperature direction needed to calculate current temperature

// - Concurrency management data -

// - IPC data -

int rcv_commands_fd;
int snd_responses_fd;

int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv);

    start_device_fifos(id, &rcv_commands_fd, &snd_responses_fd, NULL);

    struct sigaction action_handler;
    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);

    while(true) {
        // TODO: receive and execute commands, send responses
    }

    handle_shutdown();
}

void handle_shutdown() {
    error_code_t error_code = end_device_fifos(id, rcv_commands_fd, snd_responses_fd, NO_FILE_DESCRIPTOR);
    if(error_code != OK) {
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    // TODO: add here things to do on deletion
    exit(error_code);
}