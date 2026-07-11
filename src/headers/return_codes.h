/**
 * This file contains constant definitions and function declaration for return codes, both success and errors
 */

#ifndef DOMOTICS_RETURN_CODES_H
#define DOMOTICS_RETURN_CODES_H

// -- SUCCESS CODES --
#define OK                   0x00

// -- ERROR CODES --
#define ERROR_MASK           0xF0

// TODO: Find and add all possible errors, categorizing them
// ? The error subdivision can be changed if needed

// - CAUSED BY USER -
#define USER_ERROR           0x10
#define INVALID_COMMAND      0x11
#define DEVICE_TYPE_MISMATCH 0x12
#define DEVICE_NOT_FOUND     0x13

// - CAUSE RELATED TO PROCESSES -
#define PROCESS_ERROR        0x20

// - CAUSE RELATED TO THREADS -
#define THREAD_ERROR         0x30

// - CAUSE RELATED TO IPC -
#define IPC_ERROR            0x40

// Macros for error checking
#define IS_ERROR(e)         (e & ERROR_MASK) != OK
#define IS_USER_ERROR(e)    (e & ERROR_MASK) == USER_ERROR
#define IS_PROCESS_ERROR(e) (e & ERROR_MASK) == PROCESS_ERROR
#define IS_THREAD_ERROR(e)  (e & ERROR_MASK) == THREAD_ERROR
#define IS_IPC_ERROR(e)     (e & ERROR_MASK) == IPC_ERROR

// Macro for conversion from negative return value to corresponding error code
#define ERROR_FROM_RETURN(ret) -ret

// Functions for error handling

/**
 * Prints user-friendly error message
 * Command related errors should be sent up to the Controller and printed by it
 * Fatal error can be printed by the device itself
 * @param fd File descriptor to which the error should be printed
 * @param error_code Positive error code
 * 
 * TODO: Complete the printing for all possible errors
 */
void printError(int fd, int error_code);

#endif
