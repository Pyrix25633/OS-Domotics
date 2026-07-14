#define _XOPEN_SOURCE 700

#include "return_codes.h"

#include <stdio.h>
#include <errno.h>

void print_error(int fd, int error_code, int device_id, char *message) {
    if(IS_USER_ERROR(error_code)) {
        dprintf(fd, "User");
    } else if(IS_APPLICATION_ERROR(error_code)) {
        dprintf(fd, "Application");
    } else if(IS_PROCESS_ERROR(error_code)) {
        dprintf(fd, "Process related");
    } else if(IS_THREAD_ERROR(error_code)) {
        dprintf(fd, "Thread related");
    } else if(IS_IPC_ERROR(error_code)) {
        dprintf(fd, "IPC related");
    } else {
        dprintf(fd, "Unknown");
    }
    dprintf(fd, " error: 0x%2x ", error_code);
    switch(error_code) {
        case INVALID_COMMAND:          dprintf(fd, "invalid command");          break;
        case DEVICE_TYPE_MISMATCH:     dprintf(fd, "device type mismatch");     break;
        case DEVICE_NOT_FOUND:         dprintf(fd, "device not found");         break;

        case MISSING_ID_ARGUMENT:      dprintf(fd, "missing ID argument");      break;
        case CODE_FORMAT_ERROR:        dprintf(fd, "code format error");        break;
        case REQUEST_FORMAT_ERROR:     dprintf(fd, "request format error");     break;
        case RESPONSE_FORMAT_ERROR:    dprintf(fd, "response forma error");     break;
        case BUFFER_TOO_SHORT:         dprintf(fd, "buffer too short");         break;
        case INVALID_REQUEST_ARGUMENT: dprintf(fd, "invalid request argument"); break;

        case UNABLE_TO_OPEN_PIPE:      dprintf(fd, "unable to open pipe");      break;
        case UNABLE_TO_CREATE_PIPE:    dprintf(fd, "unable to create pipe");    break;
        case UNABLE_TO_CLOSE_PIPE:     dprintf(fd, "unable to close pipe");     break;
        case UNABLE_TO_REMOVE_PIPE:    dprintf(fd, "unable to remove pipe");    break;
        case UNABLE_TO_READ_PIPE:      dprintf(fd, "unable to read pipe");      break;
        case UNABLE_TO_WRITE_PIPE:     dprintf(fd, "unable to write pipe");     break;

        default:                       dprintf(fd, "no additional information");
    }
    if(error_code >= PROCESS_ERROR) {
        dprintf(fd, ", errno: %d", errno);
    }
    dprintf(fd, ", source: ");
    switch(device_id) {
        case NO_ID:                 dprintf(fd, "no ID");                      break;
        case MANUAL_INTERACTION_ID: dprintf(fd, "manual interaction program"); break;
        case CONTROLLER_ID:         dprintf(fd, "controller");                 break;
        default:                    dprintf(fd, "device %u", device_id);
    }
    if(message != NULL) {
        dprintf(fd, ", %s", message);
    }
    dprintf(fd, "\n");
}