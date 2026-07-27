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

// - CAUSED BY USER -

#define USER_ERROR               0x10
#define INVALID_TARGET_ID        0x11
#define INVALID_COMMAND          0x12
#define INVALID_COMMAND_ARGUMENT 0x13
#define DEVICE_TYPE_MISMATCH     0x14
#define DEVICE_NOT_FOUND         0x15
#define LINKING_PARENT_TO_CHILD  0x16
#define CANNOT_ADD_TO_PARENT     0x17
#define UNEXPECTED_SHUTDOWN      0x18
#define SYSTEM_OFF               0x19

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
#define ROUTE_NOT_FOUND          0x29
#define CHILD_NOT_FOUND          0x2a
#define CHILD_ERROR              0x2b
#define UNABLE_TO_CREATE_WINDOWS 0x2c
#define REGISTRY_FORMAT_ERROR    0x2d

// - CAUSE RELATED TO PROCESSES -

#define PROCESS_ERROR            0x30
#define UNABLE_TO_ALLOCATE_HEAP  0x31
#define UNABLE_TO_SET_SIGHANDLER 0x32
#define UNABLE_TO_FORK           0x33
#define UNABLE_TO_EXEC           0x34
#define UNABLE_TO_SEND_SIGNAL    0x35
#define UNABLE_TO_WAIT           0x36

// - CAUSE RELATED TO THREADS -

#define THREAD_ERROR             0x40
#define UNABLE_TO_CREATE_THREAD  0x41
#define UNABLE_TO_JOIN_THREAD    0x42
#define UNABLE_TO_CANCEL_THREAD  0x43
#define UNABLE_TO_LOCK_MUTEX     0x44
#define UNABLE_TO_UNLOCK_MUTEX   0x45

// - CAUSE RELATED TO IPC -

#define IPC_ERROR                0x50
#define UNABLE_TO_OPEN_PIPE      0x51
#define UNABLE_TO_CREATE_PIPE    0x52
#define UNABLE_TO_CLOSE_PIPE     0x53
#define UNABLE_TO_REMOVE_PIPE    0x54
#define UNABLE_TO_READ_PIPE      0x55
#define UNABLE_TO_WRITE_PIPE     0x56
#define UNEXPECTED_END_OF_FILE   0x57
#define BROKEN_PIPE              0x58
#define UNABLE_TO_SET_FD_ATTR    0x59
#define UNABLE_TO_RESTORE_STDERR 0x5a // Impossible to print to stderr

// - CAUSE RELATED TO FILES -
#define FILE_ERROR               0x60
#define UNABLE_TO_OPEN_FILE      0x61
#define UNABLE_TO_CLOSE_FILE     0x62
#define UNABLE_TO_WRITE_FILE     0x63
#define UNABLE_TO_RENAME_FILE    0x64
#define UNABLE_TO_REMOVE_FILE    0x65

// Macros for error checking

#define IS_ERROR(e)              ((e & ERROR_MASK) != OK)
#define IS_USER_ERROR(e)         ((e & ERROR_MASK) == USER_ERROR)
#define IS_APPLICATION_ERROR(e)  ((e & ERROR_MASK) == APPLICATION_ERROR)
#define IS_PROCESS_ERROR(e)      ((e & ERROR_MASK) == PROCESS_ERROR)
#define IS_THREAD_ERROR(e)       ((e & ERROR_MASK) == THREAD_ERROR)
#define IS_IPC_ERROR(e)          ((e & ERROR_MASK) == IPC_ERROR)
#define IS_FILE_ERROR(e)         ((e & ERROR_MASK) == FILE_ERROR)

#define IS_RETURN_ERROR(ret)   ((ret) < 0) // Macro for error checking of negative return values
#define ERROR_FROM_RETURN(ret) (-ret) // Macro for conversion from negative return value to corresponding error code

// Constants

#define ERROR_TYPE_SIZE   24
#define ERROR_INFO_SIZE   32
#define ERROR_CODE_SIZE   16
#define ERROR_SOURCE_SIZE 32
#define ERROR_MSG_SIZE    64

/**
 * Prints user-friendly error message
 * 
 * Command related errors should be sent up to the Controller and printed by it
 * 
 * Fatal errors can be printed by the device itself
 * 
 * A single `dprintf` call is used, so it should be atomic and error messages should not interleave each other
 * 
 * @param fd File descriptor to which the error should be printed
 * @param error_code Positive error code
 * @param device_id Id of the device that detected the error, `MANUAL_INTERACTION_ID`
 * can be used to state that the error was detected by the Manual Interaction Program
 * @param message Additional message, can state where the error was generated, can be NULL,
 * if not it must be terminated with `\0` (automatic in most cases)
 */
void print_error(int fd, int error_code, int device_id, char *message);

/**
 * Sets type and info strings for error printing
 * 
 * @param error_code Positive error code
 * @param type String where to write the type, of size `ERROR_TYPE_SIZE`
 * @param type String where to write the info, of size `ERROR_INFO_SIZE`
 */
void set_error_type_info(error_code_t error_code, char type[ERROR_TYPE_SIZE], char info[ERROR_INFO_SIZE]);

#endif
