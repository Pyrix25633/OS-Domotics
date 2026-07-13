#include "return_codes.h"

#define _XOPEN_SOURCE 700
#include <stdio.h>

void print_error(int fd, int error_code, device_id_t device_id, char *message) {
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
        case INVALID_COMMAND:       dprintf(fd, "invalid command");           break;
        case DEVICE_TYPE_MISMATCH:  dprintf(fd, "device type mismatch");      break;
        case DEVICE_NOT_FOUND:      dprintf(fd, "device not found");          break;
        case COMMAND_FORMAT_ERROR:  dprintf(fd, "command format error");      break;
        case RESPONSE_FORMAT_ERROR: dprintf(fd, "response forma error");      break;
        case BUFFER_TOO_SHORT:      dprintf(fd, "buffer too short");          break;
        default:                    dprintf(fd, "no additional information");
    }
    dprintf(fd, ", source: ");
    switch(device_id) {
        case MANUAL_INTERACTION_ID: dprintf(fd, "manual interaction program"); break;
        case CONTROLLER_ID:         dprintf(fd, "controller");                 break;
        default:                    dprintf(fd, "device %u", device_id);
    }
    if(message != NULL) {
        dprintf(fd, ", %s", message);
    }
    dprintf(fd, "\n");
}