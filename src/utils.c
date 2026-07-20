#define _XOPEN_SOURCE 700

#include "utils.h"
#include "return_codes.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

size_t string_length(char *string, size_t max_length) {
    unsigned i;
    for(i = 0; i < max_length; i++) {
        if(string[i] == '\0') {
            return i;
        }
    }
    return i;
}

int string_to_unsigned(char *string) {
    unsigned code = 0;
    for(unsigned i = 0; string[i] != '\0' && string[i] != '\n'; i++) {
        char digit = string[i];
        if(digit < '0' || digit > '9') {
            return -CODE_FORMAT_ERROR;
        }
        code *= 10;
        code += digit - '0';
    }
    return code;
}

int get_id_from_arguments(int argc, char *argv[]) {
    if(argc >= 2) {
        // The argument 0 is always the executable file name
        int parsed = string_to_unsigned(argv[1]);
        if(!(IS_RETURN_ERROR(parsed))) {
            return parsed;
        }
    }
    print_error(STDERR_FILENO, MISSING_ID_ARGUMENT, NO_ID, "in get_id_from_arguments");
    exit(MISSING_ID_ARGUMENT);
}

void set_signal_handler(int signal, void (*signal_handler)()) {
    struct sigaction action;
    action.sa_handler = signal_handler;
    action.sa_flags = SA_RESTART;
    if(sigaction(signal, &action, NULL) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_SET_SIGHANDLER, NO_ID, "while setting signal handler");
        exit(UNABLE_TO_SET_SIGHANDLER);
    }
}

error_code_t create_fifo_name(device_id_t device_id, pipe_direction_t direction, char* buffer, size_t size) {
    int length = snprintf(buffer, size, "./ipc/%u_%s.fifo", device_id, direction == DIRECTION_DOWN ? "down" : "up");
    if(length < 0) {
        return CODE_FORMAT_ERROR;
    }
    if(length >= (int)size) {
        return BUFFER_TOO_SHORT;
    }
    return OK;
}

void start_device_fifos(device_id_t device_id, int *rcv_requests_fd, int *snd_responses_fd, int *rcv_responses_fd) {
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(device_id, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (*rcv_requests_fd = open(name, O_RDONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, device_id, "opening the device pipe to receive commands");
        exit(UNABLE_TO_OPEN_PIPE);
    }
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || (*snd_responses_fd = open(name, O_WRONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, device_id, "opening the controller pipe to send responses");
        exit(UNABLE_TO_OPEN_PIPE);
    }
    if(rcv_responses_fd != NULL) {
        if(IS_ERROR(create_fifo_name(device_id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
            || mkfifo(name, PIPE_PERMISSIONS) < 0
            || (*rcv_responses_fd = open(name, O_RDONLY)) < 0) {
            print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, device_id, "creating the device pipe to receive responses");
            exit(UNABLE_TO_CREATE_PIPE);
        }
    }
}

error_code_t end_device_fifos(device_id_t device_id, int rcv_requests_fd, int snd_responses_fd, int rcv_responses_fd) {
    error_code_t error_code = OK;
    char name[PIPE_NAME_MAX_LENGTH];

    // Close the parent command fifo, close and delete `"./ipc/<id>_down.fifo"`
    if(close(rcv_requests_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(close(snd_responses_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    
    if(rcv_responses_fd != NO_FILE_DESCRIPTOR) {
        // Close and delete `"./ipc/<id>_up.fifo"`
        if(close(rcv_responses_fd) < 0) {
            error_code = UNABLE_TO_CLOSE_PIPE;
        }
        if(IS_ERROR(create_fifo_name(device_id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
            || remove(name) < 0) {
            error_code = UNABLE_TO_REMOVE_PIPE;
        }
    }
    return error_code;
}

error_code_t change_snd_responses_pipe(device_id_t parent_id, int *snd_responses_fd) {
    int old_fd = *snd_responses_fd;
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(parent_id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || (*snd_responses_fd = open(name, O_WRONLY)) < 0) {
        return UNABLE_TO_OPEN_PIPE;
    }
    if(close(old_fd) < 0) {
        return UNABLE_TO_CLOSE_PIPE;
    }
    return OK;
}

void simulate_processing_time() {
    unsigned time = rand() % (MAX_PROCESSING_TIME - MIN_PROCESSING_TIME + 1) + MIN_PROCESSING_TIME;
    sleep(time);
}

int parse_time(char *time) {
    char *last;
    char *token = strtok_r(time, ":", &last);
    int hours;
    if(token == NULL || IS_RETURN_ERROR(hours = string_to_unsigned(token)) || hours >= 24) {
        return -CODE_FORMAT_ERROR;
    }
    token = strtok_r(NULL, ":", &last);
    int minutes;
    if(token == NULL || strlen(token) != 2 || IS_RETURN_ERROR(minutes = string_to_unsigned(token)) || minutes >= 60) {
        return -CODE_FORMAT_ERROR;
    }
    token = strtok_r(NULL, ":", &last);
    if(token != NULL) { // Time string continues
        return -CODE_FORMAT_ERROR;
    }
    return minutes + (hours * 60);
}