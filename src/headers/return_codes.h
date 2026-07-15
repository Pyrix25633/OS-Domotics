/**
 * This file contains constant definitions and function declaration for return codes, both success and errors
 */

#ifndef DOMOTICS_RETURN_CODES_H
#define DOMOTICS_RETURN_CODES_H

#include "devices.h"

#include <sys/types.h>

typedef u_int8_t error_code_t;

// -- SUCCESS CODES --

#define OK                      0x00

// -- ERROR CODES --

#define ERROR_MASK              0xF0

// TODO: Find and add all possible errors, categorizing them
// ? The error subdivision can be changed if needed

// - CAUSED BY USER -

#define USER_ERROR               0x10
#define INVALID_COMMAND          0x11
#define DEVICE_TYPE_MISMATCH     0x12
#define DEVICE_NOT_FOUND         0x13

// - CAUSED BY THE APPLICATION - (should never happen)

#define APPLICATION_ERROR        0x20
#define MISSING_ID_ARGUMENT      0x21
#define CODE_FORMAT_ERROR        0x22
#define REQUEST_FORMAT_ERROR     0x23
#define RESPONSE_FORMAT_ERROR    0x24
#define BUFFER_TOO_SHORT         0x25
#define DESTINATION_ID_MISMATCH  0x26
#define INVALID_REQUEST_ARGUMENT 0x27
#define UNEXPECTED_COMMAND       0x28

// - CAUSE RELATED TO PROCESSES -

#define PROCESS_ERROR            0x30

// - CAUSE RELATED TO THREADS -

#define THREAD_ERROR             0x40
#define UNABLE_TO_CREATE_THREAD  0x41
#define UNABLE_TO_CANCEL_THREAD  0x42
#define UNABLE_TO_LOCK_MUTEX     0x43
#define UNABLE_TO_UNLOCK_MUTEX   0x44

// - CAUSE RELATED TO IPC -

#define IPC_ERROR                0x50
#define UNABLE_TO_OPEN_PIPE      0x51
#define UNABLE_TO_CREATE_PIPE    0x52
#define UNABLE_TO_CLOSE_PIPE     0x53
#define UNABLE_TO_REMOVE_PIPE    0x54
#define UNABLE_TO_READ_PIPE      0x55
#define UNABLE_TO_WRITE_PIPE     0x56

// Macros for error checking

#define IS_ERROR(e)              (e & ERROR_MASK) != OK
#define IS_USER_ERROR(e)         (e & ERROR_MASK) == USER_ERROR
#define IS_APPLICATION_ERROR(e)  (e & ERROR_MASK) == APPLICATION_ERROR
#define IS_PROCESS_ERROR(e)      (e & ERROR_MASK) == PROCESS_ERROR
#define IS_THREAD_ERROR(e)       (e & ERROR_MASK) == THREAD_ERROR
#define IS_IPC_ERROR(e)          (e & ERROR_MASK) == IPC_ERROR

#define IS_RETURN_ERROR(ret)   ret < 0 // Macro for error checking of negative return values
#define ERROR_FROM_RETURN(ret) -ret // Macro for conversion from negative return value to corresponding error code

/**
 * Prints user-friendly error message
 * 
 * Command related errors should be sent up to the Controller and printed by it
 * 
 * Fatal errors can be printed by the device itself
 * 
 * @param fd File descriptor to which the error should be printed
 * @param error_code Positive error code
 * @param device_id Id of the device that detected the error, `MANUAL_INTERACTION_ID`
 * can be used to state that the error was detected by the Manual Interaction Program
 * @param message Additional message, can state where the error was generated, can be NULL,
 * if not it must be terminated with `\0` (automatic in most cases)
 * 
 * TODO: Complete the printing for all possible errors
 */
void print_error(int fd, int error_code, int device_id, char *message);

#endif
