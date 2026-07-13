/**
 * This file contains type definitions, constant definitions and function declarations specific to the Fridge
 */

#ifndef DOMOTICS_FRIDGE_H
#define DOMOTICS_FRIDGE_H

#include "return_codes.h"

// Data types

typedef bool temperature_direction_t;

#define TEMPERATURE_RISING            false
#define TEMPERATURE_DROPPING          true

// Default values

#define DEFAULT_AUTOCLOSE_DELAY       5 // Number of seconds after which the fridge automatically closes
#define DEFAULT_FILL_PERCENTAGE       0
#define DEFAULT_THERMOSTAT            4 // Target temperature in degrees (Celsius)
#define INITIAL_SECONDS_OPEN          0
#define INITIAL_TEMPERATURE           DEFAULT_THERMOSTAT
#define INITIAL_TEMPERATURE_DIRECTION TEMPERATURE_RISING

// Temperature related constants

#define TEMPERATURE_THRESHOLD         2 // Number of degrees from the thermostat after which the fridge starts cooling
#define CLOSED_TEMPERATURE_INCREASE   1 // How fast the temperature increases when the fridge is closed
#define CLOSED_TEMPERATURE_DECREASE   3 // How fast the temperature increases when the fridge is closed and cooling
#define OPEN_TEMPERATURE_INCREASE     3 // How fast the temperature increases when the fridge is open
#define AMBIENT_TEMPERATURE           26

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
