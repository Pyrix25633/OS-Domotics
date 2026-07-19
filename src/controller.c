#define _XOPEN_SOURCE 700

#include "controller.h"

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely
// pthread_t autoclose_thread; // TODO: add threads

// - IPC data -

int rcv_responses_fd;
bool force_exit = false;
char request_buffer[MAX_REQUEST_SIZE];
char response_buffer[MAX_RESPONSE_SIZE];
request_t request;
response_t response;
routing_table_t routing_table;

// - Signal handler -

struct sigaction action_handler;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);

    start_responses_fifo();

    init_routing_table(routing_table);

    handle_shutdown(OK); // TODO: change passed error code
}

void start_responses_fifo() {
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || mkfifo(name, PIPE_PERMISSIONS) < 0
        || (rcv_responses_fd = open(name, O_RDONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, CONTROLLER_ID, "creating the pipe to receive responses");
        exit(UNABLE_TO_CREATE_PIPE);
    }
}

error_code_t end_responses_fifo() {
    error_code_t error_code = OK;
    char name[PIPE_NAME_MAX_LENGTH];    
    if(close(rcv_responses_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || remove(name) < 0) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    return error_code;
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = end_responses_fifo();
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, CONTROLLER_ID, "while closing and deleting pipes");
    }
    // TODO: join or cancel threads
    /*if(state == STATE_OPEN && pthread_cancel(autoclose_thread) != 0) {
        error_code = UNABLE_TO_CANCEL_THREAD;
        print_error(STDERR_FILENO, error_code, id, "in shutdown");
    }*/
    if(!IS_ERROR(error_code)) {
        error_code = error;
    }
    exit(error_code);
}

void sigterm_handler() {
    handle_shutdown(UNEXPECTED_SHUTDOWN);
}