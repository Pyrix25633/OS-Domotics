#define _XOPEN_SOURCE 700

#include "controller.h"
#include "devices.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

int main(int argc, char *argv[]) {
    print_error(STDERR_FILENO, UNEXPECTED_END_OF_FILE, CONTROLLER_ID, NULL);
    print_error(STDERR_FILENO, UNABLE_TO_CANCEL_THREAD, CONTROLLER_ID, "with test message");
    print_error(STDERR_FILENO, INVALID_COMMAND, 15, "with test message");
    print_error(STDERR_FILENO, REQUEST_FORMAT_ERROR, NO_ID, NULL);

    return OK;
}