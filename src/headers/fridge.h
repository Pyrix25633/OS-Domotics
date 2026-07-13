/**
 * This file contains type definitions, constant definitions and function declarations specific to the Fridge
 */

#ifndef DOMOTICS_FRIDGE_H
#define DOMOTICS_FRIDGE_H

#include "return_codes.h"

/**
 * Main function of the Fridge Program
 * 
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * TODO: Determine all other possible exit values, add them every time you find out another error that requires complete
 * termination of the process can occur
 * 
 * @returns `OK`
 */
int main(int argc, char *argv[]);

/**
 * Handles the shutdown also cleaning up IPC files
 */
void handle_shutdown();

#endif
