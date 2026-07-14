#define _XOPEN_SOURCE 700

#include "utils.h"
#include "return_codes.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

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
    for(unsigned i = 0; string[i] != '\0'; i++) {
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

error_code_t create_fifo_name(device_id_t device_id, bool down, char* buffer, size_t size) {
    int length = snprintf(buffer, size, "./ipc/%u_%s.fifo", device_id, down ? "down" : "up");
    if(length < 0) {
        return CODE_FORMAT_ERROR;
    }
    if(length >= size) {
        return BUFFER_TOO_SHORT;
    }
    return OK;
}

void start_device_fifos(device_id_t device_id, int *rcv_commands_fd, int *snd_responses_fd, int *rcv_responses_fd) {
    char name[PIPE_NAME_MAX_LENGTH];
    if(create_fifo_name(device_id, true, name, PIPE_NAME_MAX_LENGTH) != OK
        || mkfifo(name, PIPE_PERMISSIONS) < 0
        || (*rcv_commands_fd = open(name, O_RDONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, device_id, "creating the device pipe to receive commands");
        exit(UNABLE_TO_CREATE_PIPE);
    }
    if(create_fifo_name(CONTROLLER_ID, false, name, PIPE_NAME_MAX_LENGTH) != OK
        || (*snd_responses_fd = open(name, O_WRONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, device_id, "opening the controller pipe to send responses");
        exit(UNABLE_TO_OPEN_PIPE);
    }
    if(rcv_responses_fd != NULL) {
        if(create_fifo_name(device_id, false, name, PIPE_NAME_MAX_LENGTH) != OK
            || mkfifo(name, PIPE_PERMISSIONS) < 0
            || (*rcv_responses_fd = open(name, O_RDONLY)) < 0) {
            print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, device_id, "creating the device pipe to receive commands");
            exit(UNABLE_TO_CREATE_PIPE);
        }
    }
}

error_code_t end_device_fifos(device_id_t device_id, int rcv_commands_fd, int snd_responses_fd, int rcv_responses_fd) {
    error_code_t error_code = OK;
    char name[PIPE_NAME_MAX_LENGTH];

    // Close the parent command fifo, close and delete `"./ipc/<id>_down.fifo"`
    if(close(rcv_commands_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(close(snd_responses_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(create_fifo_name(device_id, true, name, PIPE_NAME_MAX_LENGTH) != OK
        || remove(name) < 0) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    
    if(rcv_responses_fd != NO_FILE_DESCRIPTOR) {
        // Close and delete `"./ipc/<id>_up.fifo"`
        if(close(rcv_responses_fd) < 0) {
            error_code = UNABLE_TO_CLOSE_PIPE;
        }
        if(create_fifo_name(device_id, false, name, PIPE_NAME_MAX_LENGTH) != OK
            || remove(name) < 0) {
            error_code = UNABLE_TO_REMOVE_PIPE;
        }
    }
    return error_code;
}

error_code_t change_snd_responses_pipe(device_id_t parent_id, int *snd_responses_fd) {
    if(close(*snd_responses_fd) < 0) {
        return UNABLE_TO_CLOSE_PIPE;
    }
    char *name[PIPE_NAME_MAX_LENGTH];
    if(create_fifo_name(parent_id, false, name, PIPE_NAME_MAX_LENGTH) != OK
        || (*snd_responses_fd = open(name, O_WRONLY)) < 0) {
        return UNABLE_TO_OPEN_PIPE;
    }
}