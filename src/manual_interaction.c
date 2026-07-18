#define _XOPEN_SOURCE 700

#include "manual_interaction.h"

int main(int argc, char *argv[]) {
    user_command_t user_command;
    error_code_t error_code = parse_user_command(&user_command, argc, argv);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while parsing command");
        exit(error_code);
    }

    // TODO

    return OK;
}

error_code_t parse_user_command(user_command_t *user_command, int argc, char *argv[]) {
    // First argument is always the executable name
    int target;
    if(argc < 2 || IS_RETURN_ERROR(target = string_to_unsigned(argv[1]))) {
        return INVALID_TARGET_ID;
    }
    user_command->target = target;
    if(argc < 3) {
        return INVALID_COMMAND;
    }
    char *command = argv[2];
    if(strcmp(command, "switch") == 0) {
        return parse_switch_command(user_command, argc, argv);
    }
    else if(strcmp(command, "set") == 0) {
        return parse_set_command(user_command, argc, argv);
    }
    else if(strcmp(command, "info") == 0) {
        user_command->code = INFO_COMMAND;
        return OK;
    }
    return INVALID_COMMAND;
}

error_code_t parse_switch_command(user_command_t *user_command, int argc, char *argv[]) {
    user_command->code = SWITCH_COMMAND;
    if(argc < 5) {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *label = argv[3];
    command_code_t message_code = SWITCH;
    if(strcmp(label, "power") == 0) {
        message_code |= SWITCH_POWER;
    }
    else if(strcmp(label, "open") == 0) {
        message_code |= SWITCH_OPEN;
    }
    else if(strcmp(label, "close") == 0) {
        message_code |= SWITCH_CLOSE;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *position = argv[4];
    if(strcmp(position, "on") == 0) {
        message_code |= POSITION_ON;
    }
    else if(strcmp(position, "off") == 0) {
        message_code |= POSITION_OFF;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    user_command->message_code = message_code;
    return OK;
}

error_code_t parse_set_command(user_command_t *user_command, int argc, char *argv[]) {
    user_command->code = SET_COMMAND;
    if(argc < 5) {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *label = argv[3];
    command_code_t message_code = REGISTRY;
    if(strcmp(label, "begin") == 0) {
        message_code |= REGISTRY_BEGIN;
    }
    else if(strcmp(label, "end") == 0) {
        message_code |= REGISTRY_END;
    }
    else if(strcmp(label, "delay") == 0) {
        message_code |= REGISTRY_DELAY;
    }
    else if(strcmp(label, "thermostat") == 0) {
        message_code |= REGISTRY_THERMOSTAT;
    }
    else if(strcmp(label, "perc") == 0) {
        message_code |= REGISTRY_PERCENTAGE;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    user_command->message_code = message_code;
    if(REGISTRY_SUBCOMMAND(message_code) == REGISTRY_BEGIN || REGISTRY_SUBCOMMAND(message_code) == REGISTRY_END) {
        // TODO
        return OK;
    }
    int argument = string_to_unsigned(argv[4]);
    if(IS_RETURN_ERROR(argument)) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(REGISTRY_SUBCOMMAND(message_code) == REGISTRY_THERMOSTAT) {
        if(argument < MIN_THERMOSTAT || argument > MAX_THERMOSTAT) {
            return INVALID_COMMAND_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(message_code) == REGISTRY_PERCENTAGE) {
        if(argument > 100) {
            return INVALID_COMMAND_ARGUMENT;
        }
    }
    user_command->argument = argument;
    return OK;
}