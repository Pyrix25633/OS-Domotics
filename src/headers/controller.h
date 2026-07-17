/**
 * This file contains type definitions, constant definitions and function declarations specific to the Controller
 */

#ifndef DOMOTICS_CONTROLLER_H
#define DOMOTICS_CONTROLLER_H

#include "devices.h"
#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include "routing.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

/**
 * Main function of the Controller Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * TODO: Determine all other possible exit values, add them every time you find out another error that requires complete
 * termination of the process can occur
 * 
 * @returns `OK`
 */
int main(int argc, char *argv[]);

#endif
