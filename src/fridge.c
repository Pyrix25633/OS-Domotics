#define _XOPEN_SOURCE 700

#include "fridge.h"

// - Explicit device data -

device_id_t id; // Never changes
leaf_device_state_t state = STATE_CLOSED; // Modified by switch commands
u_int8_t autoclose_delay = DEFAULT_AUTOCLOSE_DELAY; // Can be modified
u_int8_t fill_percentage = DEFAULT_FILL_PERCENTAGE; // Can be modified only manually
u_int8_t thermostat = DEFAULT_THERMOSTAT; // Can be modified only manually

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
time_t last_opened; // Timestamp needed to calculate the open time and current temperature
time_t last_closed; // Timestamp needed to calculate the open time and current temperature
time_t last_thermostat_set; // Timestamp needed to calculate the current temperature
float last_temperature = INITIAL_TEMPERATURE; // Used to calculate the current temperature

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely
pthread_t autoclose_thread;

// - IPC data -

int rcv_requests_fd;
int snd_responses_fd;
bool force_exit = false;
char request_buffer[MAX_REQUEST_SIZE];
char response_buffer[MAX_RESPONSE_SIZE];
request_t request;
response_t response;

// - Signal handler -

struct sigaction action_handler;

int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv);
    response.source = id;
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);

    last_closed = last_opened = last_thermostat_set = time(NULL);

    srand(last_closed); // Seed random generator with current time, always different

    while(!force_exit) {
        execute_command();
    }

    handle_shutdown();
}

void execute_command() {
    response.arguments_size = 0;
    
    error_code_t error_code = read_pipe();
    if(error_code != OK) {
        response.command_code = NULL_COMMAND;
        response.response_code = error_code;
    }
    else if(request.destination != id) {
        response.response_code = DESTINATION_ID_MISMATCH;
    }
    else {
        response.command_code = request.command_code;
        response.response_code = OK;

        if(pthread_mutex_lock(&data_mutex) < 0) {
            response.response_code = UNABLE_TO_LOCK_MUTEX;
        } else {
            if(IS_INFO(request.command_code)) { create_info_response(); }
            else if(IS_LINK(request.command_code)) { create_link_response(); }
            else if(IS_DELETE(request.command_code)) { force_exit = true; }
            else if(IS_REGISTRY(request.command_code)) { create_registry_response(); }
            else if(IS_SWITCH(request.command_code)) { create_switch_response(); }
            else {
                response.response_code = UNEXPECTED_COMMAND;
            }
            if(pthread_mutex_unlock(&data_mutex) < 0) {
                print_error(STDERR_FILENO, UNABLE_TO_UNLOCK_MUTEX, id, "while processing request");
                force_exit = true; // Nothing to do, do not try to lock again
            }
        }
    }

    simulate_processing_time();

    write_pipe();
}

error_code_t read_pipe() {
    ssize_t size = read(rcv_requests_fd, request_buffer, MAX_REQUEST_SIZE);
    if(size < 0) {
        return UNABLE_TO_READ_PIPE;
    }
    if(size == 0) {
        force_exit = true;
        return UNEXPECTED_END_OF_FILE;
    }
    return parse_request(&request, request_buffer, MAX_REQUEST_SIZE);
}

void create_info_response() {
    response.arguments_size = 6;
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[OPEN_SECONDS_ARGUMENT] = (state == STATE_OPEN) ? (time(NULL) - last_opened) : (last_closed - last_opened);
    response.arguments[AUTOCLOSE_DELAY_ARGUMENT] = autoclose_delay;
    response.arguments[FILL_PERCENTAGE_ARGUMENT] = fill_percentage;
    response.arguments[THERMOSTAT_ARGUMENT] = thermostat;
    response.arguments[TEMPERATURE_ARGUMENT] = calculate_current_temperature() * 10;
}

void create_link_response() {
    if(LINK_SUBCOMMAND(request.command_code) == LINK_CHANGE_PARENT) {
        device_id_t new_parent_id = request.arguments[PARENT_ID_ARGUMENT];
        response.arguments_size = 1;
        response.arguments[REQUEST_ARGUMENT] = new_parent_id;
        if(new_parent_id != parent_id) {
            response.response_code = change_snd_responses_pipe(new_parent_id, &snd_responses_fd);
            if(response.response_code == OK) {
                parent_id = new_parent_id;
            }
        }
    }
    else {
        response.response_code = UNEXPECTED_COMMAND;
    }
}

void create_registry_response() {
    u_int16_t registry = request.arguments[REGISTRY_ARGUMENT];
    response.arguments_size = 1;
    response.arguments[REQUEST_ARGUMENT] = registry;
    if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_DELAY) {
        autoclose_delay = registry;
    }
    else if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_THERMOSTAT) {
        if(registry >= MIN_THERMOSTAT && registry <= MAX_THERMOSTAT) {
            last_temperature = calculate_current_temperature();
            last_thermostat_set = time(NULL);
            thermostat = registry;
        }
        else {
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(request.command_code) == REGISTRY_PERCENTAGE) {
        if(registry <= MAX_FILL_PERCENTAGE) {
            fill_percentage = registry;
        }
        else {
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else {
        response.arguments_size = 0;
        response.response_code = UNEXPECTED_COMMAND;
    }
}

void create_switch_response() {
    if(SWITCH_LABEL(request.command_code) == SWITCH_OPEN) {
        if(SWITCH_POSITION(request.command_code) == POSITION_ON) {
            response.response_code = set_state(STATE_OPEN, false);
        }
    }
    else if(SWITCH_LABEL(request.command_code) == SWITCH_CLOSE) {
        if(SWITCH_POSITION(request.command_code) == POSITION_ON) {
            response.response_code = set_state(STATE_CLOSED, false);
        }
    }
    else {
        response.response_code = UNEXPECTED_COMMAND;
    }
}

void write_pipe() {
    error_code_t error_code = format_response(&response, response_buffer, MAX_RESPONSE_SIZE);
    
    if(error_code != OK) {
        print_error(STDERR_FILENO, error_code, id, "while formatting response");
    }
    else if(write(snd_responses_fd, response_buffer, MAX_RESPONSE_SIZE) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

void handle_shutdown() {
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, NO_FILE_DESCRIPTOR);
    if(error_code != OK) {
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    if(state == STATE_OPEN && pthread_cancel(autoclose_thread) != 0) {
        error_code = UNABLE_TO_CANCEL_THREAD;
        print_error(STDERR_FILENO, error_code, id, "in shutdown");
    }
    exit(error_code);
}

error_code_t set_state(leaf_device_state_t new_state, bool automatic) {
    if(new_state != state) {
        if(new_state == STATE_OPEN) {
            last_opened = time(NULL);
            pthread_attr_t attributes;
            if(pthread_attr_init(&attributes) != 0
                || pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE) != 0
                || pthread_create(&autoclose_thread, &attributes, autoclose_routine, NULL) != 0) {
                return UNABLE_TO_CREATE_THREAD;
            }
        }
        else {
            last_closed = time(NULL);
            if(!automatic && pthread_cancel(autoclose_thread) < 0) { // The thread does not cancel itself
                return UNABLE_TO_CANCEL_THREAD;
            }
        }
        last_temperature = calculate_current_temperature();
        state = new_state;
    }
    return OK;
}

void* autoclose_routine(void *arg) {
    (void)arg; // Unused parameter
    sleep(autoclose_delay);

    if(pthread_mutex_lock(&data_mutex) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_LOCK_MUTEX, id, "in autoclose thread");
        pthread_exit(NULL);
    }

    set_state(STATE_CLOSED, true);

    if(pthread_mutex_unlock(&data_mutex) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_LOCK_MUTEX, id, "in autoclose thread");
        handle_shutdown();
    }

    char response_buffer[MAX_RESPONSE_SIZE];
    response_t response;
    response.source = id;
    response.command_code = SWITCH | SWITCH_CLOSE | POSITION_ON;
    response.response_code = OK;
    response.arguments_size = 0;
    error_code_t error_code = format_response(&response, response_buffer, MAX_RESPONSE_SIZE);
    if(error_code != OK) {
        print_error(STDERR_FILENO, error_code, id, "in autoclose thread");
    } else if(write(snd_responses_fd, response_buffer, MAX_RESPONSE_SIZE) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "in autoclose thread");
    }

    pthread_exit(NULL);
}

float calculate_current_temperature() {
    float current_temperature;
    time_t elapsed_time;
    if(state == STATE_OPEN) {
        elapsed_time = time(NULL) - (last_opened > last_thermostat_set ? last_opened : last_thermostat_set);
        current_temperature = last_temperature + (elapsed_time * OPEN_INCREASE_SLOPE);
        return current_temperature > AMBIENT_TEMPERATURE ? AMBIENT_TEMPERATURE : current_temperature;
    }
    else {
        elapsed_time = time(NULL) - (last_closed > last_thermostat_set ? last_closed : last_thermostat_set);
        /*
         For simplicity, if the fridge is closed, the cycle starts after the fridge is cooled to the minimum temperature
         The same is if the thermostat is changed
        */
        time_t to_range_time = 0;
        current_temperature = last_temperature;
        if(last_temperature > MIN_TEMPERATURE(thermostat)) {
            to_range_time = (last_temperature - MIN_TEMPERATURE(thermostat))/(-CLOSED_DECREASE_SLOPE);
            current_temperature = last_temperature + (elapsed_time * CLOSED_DECREASE_SLOPE);
            current_temperature = current_temperature < MIN_TEMPERATURE(thermostat) ? MIN_TEMPERATURE(thermostat) : current_temperature;
        }
        else {
            to_range_time = (MIN_TEMPERATURE(thermostat) - last_temperature)/(CLOSED_INCREASE_SLOPE);
            current_temperature = last_temperature + (elapsed_time * CLOSED_INCREASE_SLOPE);
            current_temperature = current_temperature > MIN_TEMPERATURE(thermostat) ? MIN_TEMPERATURE(thermostat) : current_temperature;
        }
        if(elapsed_time > to_range_time) {
            elapsed_time -= to_range_time;
            elapsed_time %= TOTAL_CLOSED_CYCLE_TIME;
            if(elapsed_time < CLOSED_INCREASE_TIME) {
                current_temperature = MIN_TEMPERATURE(thermostat) + (elapsed_time * CLOSED_INCREASE_SLOPE);
            }
            else {
                elapsed_time -= CLOSED_INCREASE_TIME;
                current_temperature = MAX_TEMPERATURE(thermostat) + (elapsed_time * CLOSED_DECREASE_SLOPE);
            }
        }
        return current_temperature;
    }
}