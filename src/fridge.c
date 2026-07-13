#include "fridge.h"
#include "utils.h"
#include "return_codes.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

device_id_t id;
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