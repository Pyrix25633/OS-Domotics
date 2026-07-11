#include "return_codes.h"

void printError(int fd, int error_code) {
    if(IS_USER_ERROR(error_code)) {
    } else if(IS_PROCESS_ERROR(error_code)) {
    } else if(IS_THREAD_ERROR(error_code)) {
    } else if(IS_IPC_ERROR(error_code)) {
    } else {
    }
    // TODO: print specific information
}