/**
 * This file contains type definitions, constant definitions and function declarations specific to the Fridge
 */

#ifndef DOMOTICS_FRIDGE_H
#define DOMOTICS_FRIDGE_H

#include "return_codes.h"

#include <sys/types.h>

// Default values

#define DEFAULT_AUTOCLOSE_DELAY       30 // Number of seconds after which the fridge automatically closes
#define DEFAULT_FILL_PERCENTAGE       0
#define DEFAULT_THERMOSTAT            4 // Target temperature in degrees (Celsius)
#define INITIAL_TEMPERATURE           DEFAULT_THERMOSTAT
#define INITIAL_TEMPERATURE_DIRECTION TEMPERATURE_RISING

// Temperature related constants

#define TEMPERATURE_THRESHOLD         2 // Number of degrees from the thermostat after which the fridge starts cooling
#define MIN_TEMPERATURE(t)            (t - TEMPERATURE_THRESHOLD) // Lower histheresys bound
#define MAX_TEMPERATURE(t)            (t + TEMPERATURE_THRESHOLD) // Upper histheresys bound
#define AMBIENT_TEMPERATURE           26 // Ambient temperature
#define CLOSED_INCREASE_TIME          (55*60) // Number of seconds the temperature takes to go from `thermostat - TEMPERATURE_THRESHOLD` to `thermostat + TEMPERATURE_THRESHOLD`
#define CLOSED_DECREASE_TIME          (5*60) // Number of seconds the temperature takes to go from `thermostat + TEMPERATURE_THRESHOLD` to `thermostat - TEMPERATURE_THRESHOLD`
#define TOTAL_CLOSED_CYCLE_TIME       CLOSED_INCREASE_TIME + CLOSED_DECREASE_TIME // Total time of an histheresys cycle
#define CLOSED_INCREASE_SLOPE         (float)(2 * TEMPERATURE_THRESHOLD)/CLOSED_INCREASE_TIME // How fast the temperature increases when the fridge is closed
#define CLOSED_DECREASE_SLOPE         -(float)(2 * TEMPERATURE_THRESHOLD)/CLOSED_DECREASE_TIME // How fast the temperature decreases when the fridge is closed and cooling
#define OPEN_INCREASE_TIME            (10*60) // Number of seconds the temperature takes to go from `thermostat` to `AMBIENT_TEMPERATURE`
#define OPEN_INCREASE_SLOPE           (float)(AMBIENT_TEMPERATURE - 0)/OPEN_INCREASE_TIME // How fast the temperature increases when the fridge is open

/**
 * Main function of the Fridge Program
 * 
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 * 
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_OPEN_PIPE` if the IPC pipes could not be opened,
 * `UNABLE_TO_CLOSE_PIPE` if the IPC pipes could not be closed,
 * `UNABLE_TO_CANCEL_THREAD` if the autoclose thread could not be cancelled,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Reads requests from the pipe, executes the commands and sends responses
 */
void execute_command();

/**
 * Reads the pipe and parses the request
 */
error_code_t read_pipe();

/**
 * Handles an info command and creates a response
 */
void create_info_response();

/**
 * Handles a link command and creates a response
 * 
 * If the parent does not change the pipes are not closed and reopened
 * 
 * The new parent ID is always provided as argument in the response
 * 
 * The parent ID is not updated if the pipe switch fails
 */
void create_link_response();

/**
 * Handles a registry command and creates a response
 */
void create_registry_response();

/**
 * Handles a switch command and creates a response
 * 
 * Sets the state, scheduling or cancelling the autoclose action
 */
void create_switch_response();

/**
 * Formats the response and writes it in the pipe
 * 
 * If it encounters errors, they are printed in `stderr` because they cannot be sent to the parent
 */
void write_pipe();

/**
 * Handles the shutdown
 */
void handle_shutdown();

/**
 * Sets the state and updates auxiliary variables
 * 
 * @param new_state The new state
 * @param automatic If the action is automatic, the automatic action does not cancel itself
 * 
 * @returns `UNABLE_TO_CREATE_THREAD` if the autoclose thread could not be created,
 * `UNABLE_TO_CANCEL_THREAD` if the previous autoclose thread could not be cancelled,
 * `OK` otherwise
 */
error_code_t set_state(leaf_device_state_t new_state, bool automatic);

/**
 * It's the function executed by the autoclose thread created
 * 
 * If any error occurs, it is printed on the stderr and the routine terminates, as it's a critical non-solvable error
 * 
 * If it fails releasing the lock the handle_shutdown function is closed and the device is terminated
 * 
 * If it succeeds a notification is sent to the parent
 * 
 * @param arg Not used
 * 
 * @returns `NULL`
 */
void* autoclose_routine(void *arg);

/**
 * Calculates the current temperature based on the fridge state and the last temperature
 * 
 * Used both for info and while changing state
 * 
 * It approximates linearly the temperature trend of the fridge
 * 
 * @returns The current temperature in degrees (Celsius)
 */
float calculate_current_temperature();

#endif
