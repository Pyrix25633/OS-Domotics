#define _XOPEN_SOURCE 700

#include "return_codes.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

void print_error(int fd, int error_code, int device_id, char *message) {
    char type[24];
    if(IS_USER_ERROR(error_code)) {
        strcpy(type, "User");
    } else if(IS_APPLICATION_ERROR(error_code)) {
        strcpy(type, "Application");
    } else if(IS_PROCESS_ERROR(error_code)) {
        strcpy(type, "Process related");
    } else if(IS_THREAD_ERROR(error_code)) {
        strcpy(type, "Thread related");
    } else if(IS_IPC_ERROR(error_code)) {
        strcpy(type, "IPC related");
    } else {
        strcpy(type, "Unknown");
    }
    char info[32];
    switch(error_code) {
        case INVALID_COMMAND:          strcpy(info, "invalid command");          break;
        case DEVICE_TYPE_MISMATCH:     strcpy(info, "device type mismatch");     break;
        case DEVICE_NOT_FOUND:         strcpy(info, "device not found");         break;

        case MISSING_ID_ARGUMENT:      strcpy(info, "missing ID argument");      break;
        case CODE_FORMAT_ERROR:        strcpy(info, "code format error");        break;
        case REQUEST_FORMAT_ERROR:     strcpy(info, "request format error");     break;
        case RESPONSE_FORMAT_ERROR:    strcpy(info, "response forma error");     break;
        case BUFFER_TOO_SHORT:         strcpy(info, "buffer too short");         break;
        case DESTINATION_ID_MISMATCH:  strcpy(info, "destination ID mismatch");  break;
        case INVALID_REQUEST_ARGUMENT: strcpy(info, "invalid request argument"); break;
        case UNEXPECTED_COMMAND:       strcpy(info, "unexpected command");       break;

        case UNABLE_TO_CREATE_THREAD:  strcpy(info, "unable to create thread");  break;
        case UNABLE_TO_CANCEL_THREAD:  strcpy(info, "unable to cancel thread");  break;
        case UNABLE_TO_LOCK_MUTEX:     strcpy(info, "unable to lock mutex");     break;
        case UNABLE_TO_UNLOCK_MUTEX:   strcpy(info, "unable to unlock mutex");   break;

        case UNABLE_TO_OPEN_PIPE:      strcpy(info, "unable to open pipe");      break;
        case UNABLE_TO_CREATE_PIPE:    strcpy(info, "unable to create pipe");    break;
        case UNABLE_TO_CLOSE_PIPE:     strcpy(info, "unable to close pipe");     break;
        case UNABLE_TO_REMOVE_PIPE:    strcpy(info, "unable to remove pipe");    break;
        case UNABLE_TO_READ_PIPE:      strcpy(info, "unable to read pipe");      break;
        case UNABLE_TO_WRITE_PIPE:     strcpy(info, "unable to write pipe");     break;
        case UNEXPECTED_END_OF_FILE:   strcpy(info, "unexpected end of file");   break;

        default:                       strcpy(info, "no additional information");
    }
    char code[16] = "";
    if(error_code >= PROCESS_ERROR) {
        sprintf(code, ", errno: %d", errno);
    }
    char source[32];
    switch(device_id) {
        case NO_ID:                 strcpy(source, "no ID");                      break;
        case MANUAL_INTERACTION_ID: strcpy(source, "manual interaction program"); break;
        case CONTROLLER_ID:         strcpy(source, "controller");                 break;
        default:                    sprintf(source, "device %u", device_id);
    }
    char msg[48] = "";
    if(message != NULL) {
        sprintf(msg, ", %s", message);
    }
    dprintf(fd, "%s error: 0x%2x %s%s, source: %s%s\n", type, error_code, info, code, source, msg);
}