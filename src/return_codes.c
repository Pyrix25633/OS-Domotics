#define _XOPEN_SOURCE 700

#include "return_codes.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

void print_error(int fd, int error_code, int device_id, char *message) {
    char type[ERROR_TYPE_SIZE];
    char info[ERROR_INFO_SIZE];
    set_error_type_info(error_code, type, info);
    char code[16] = "";
    if(error_code >= PROCESS_ERROR) {
        sprintf(code, ", errno: %d", errno);
    }
    char source[ERROR_SOURCE_SIZE];
    switch(device_id) {
        case NO_ID:                 strcpy(source, "no ID");                      break;
        case MANUAL_INTERACTION_ID: strcpy(source, "Manual Interaction Program"); break;
        case CONTROLLER_ID:         strcpy(source, "Controller");                 break;
        default:                    sprintf(source, "device %u", device_id);
    }
    char msg[ERROR_MSG_SIZE] = "";
    if(message != NULL) {
        sprintf(msg, ", %s", message);
    }
    if(error_code == UNABLE_TO_RESTORE_STDERR) {
        fd = STDOUT_FILENO;
    }
    dprintf(fd, "%s error: 0x%02x %s%s, source: %s%s\n", type, error_code, info, code, source, msg);
}

void set_error_type_info(error_code_t error_code, char type[ERROR_TYPE_SIZE], char info[ERROR_INFO_SIZE]) {
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
    } else if(IS_FILE_ERROR(error_code)) {
        strcpy(type, "File related");
    } else {
        strcpy(type, "Unknown");
    }
    switch(error_code) {
        case INVALID_TARGET_ID:        strcpy(info, "invalid target ID");            break;
        case INVALID_COMMAND:          strcpy(info, "invalid command");              break;
        case INVALID_COMMAND_ARGUMENT: strcpy(info, "invalid command argument");     break;
        case DEVICE_TYPE_MISMATCH:     strcpy(info, "device type mismatch");         break;
        case DEVICE_NOT_FOUND:         strcpy(info, "device not found");             break;
        case LINKING_PARENT_TO_CHILD:  strcpy(info, "linking parent to child");      break;
        case CANNOT_ADD_TO_PARENT:     strcpy(info, "cannot add to parent");         break;
        case UNEXPECTED_SHUTDOWN:      strcpy(info, "unexpected shutdown");          break;
        case SYSTEM_OFF:               strcpy(info, "system off");                   break;

        case MISSING_ID_ARGUMENT:      strcpy(info, "missing ID argument");          break;
        case CODE_FORMAT_ERROR:        strcpy(info, "code format error");            break;
        case REQUEST_FORMAT_ERROR:     strcpy(info, "request format error");         break;
        case RESPONSE_FORMAT_ERROR:    strcpy(info, "response format error");        break;
        case BUFFER_TOO_SHORT:         strcpy(info, "buffer too short");             break;
        case DESTINATION_ID_MISMATCH:  strcpy(info, "destination ID mismatch");      break;
        case INVALID_REQUEST_ARGUMENT: strcpy(info, "invalid request argument");     break;
        case UNEXPECTED_COMMAND:       strcpy(info, "unexpected command");           break;
        case ROUTE_NOT_FOUND:          strcpy(info, "route not found");              break;
        case CHILD_NOT_FOUND:          strcpy(info, "child not found");              break;
        case CHILD_ERROR:              strcpy(info, "child error");                  break;
        case UNABLE_TO_CREATE_WINDOWS: strcpy(info, "unable to create windows");     break;
        case REGISTRY_FORMAT_ERROR:    strcpy(info, "registry format error");        break;

        case UNABLE_TO_ALLOCATE_HEAP:  strcpy(info, "unable to allocate heap");      break;
        case UNABLE_TO_SET_SIGHANDLER: strcpy(info, "unable to set signal handler"); break;
        case UNABLE_TO_FORK:           strcpy(info, "unable to fork");               break;
        case UNABLE_TO_EXEC:           strcpy(info, "unable to exec");               break;
        case UNABLE_TO_SEND_SIGNAL:    strcpy(info, "unable to send signal");        break;
        case UNABLE_TO_WAIT:           strcpy(info, "unable to wait");               break;

        case UNABLE_TO_CREATE_THREAD:  strcpy(info, "unable to create thread");      break;
        case UNABLE_TO_CANCEL_THREAD:  strcpy(info, "unable to cancel thread");      break;
        case UNABLE_TO_JOIN_THREAD:    strcpy(info, "unable to join thread");        break;
        case UNABLE_TO_LOCK_MUTEX:     strcpy(info, "unable to lock mutex");         break;
        case UNABLE_TO_UNLOCK_MUTEX:   strcpy(info, "unable to unlock mutex");       break;

        case UNABLE_TO_OPEN_PIPE:      strcpy(info, "unable to open pipe");          break;
        case UNABLE_TO_CREATE_PIPE:    strcpy(info, "unable to create pipe");        break;
        case UNABLE_TO_CLOSE_PIPE:     strcpy(info, "unable to close pipe");         break;
        case UNABLE_TO_REMOVE_PIPE:    strcpy(info, "unable to remove pipe");        break;
        case UNABLE_TO_READ_PIPE:      strcpy(info, "unable to read pipe");          break;
        case UNABLE_TO_WRITE_PIPE:     strcpy(info, "unable to write pipe");         break;
        case BROKEN_PIPE:              strcpy(info, "broken pipe");                  break;
        case UNEXPECTED_END_OF_FILE:   strcpy(info, "unexpected end of file");       break;
        case UNABLE_TO_SET_FD_ATTR:    strcpy(info, "unable to set fd attributes");  break;
        case UNABLE_TO_RESTORE_STDERR: strcpy(info, "unable to restore stderr");     break;

        case UNABLE_TO_OPEN_FILE:      strcpy(info, "unable to open file");          break;
        case UNABLE_TO_CLOSE_FILE:     strcpy(info, "unable to close file");         break;
        case UNABLE_TO_READ_FILE:      strcpy(info, "unable to read file");          break;
        case UNABLE_TO_WRITE_FILE:     strcpy(info, "unable to write file");         break;
        case UNABLE_TO_RENAME_FILE:    strcpy(info, "unable to rename file");        break;
        case UNABLE_TO_REMOVE_FILE:    strcpy(info, "unable to remove file");        break;

        default:                       strcpy(info, "no additional information");
    }
}