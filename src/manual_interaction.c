#define _XOPEN_SOURCE 700

#include "manual_interaction.h"

int snd_command_fd;

int main(int argc, char *argv[]) {
    // Parse user command

    user_command_t user_command;
    error_code_t error_code = parse_user_command(&user_command, argc, argv);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while parsing command");
        exit(error_code);
    }

    /*
     `user_command.code` is not used here, but to avoid possible inconsistencies it is
     set by `parse_user_command` anyway
    */

    // TODO: check that target ID exists and device type matches

    // Send command request to target device

    open_device_pipe(user_command.target);

    request_t request;
    request.destination = user_command.target;
    request.command_code = user_command.message_code;
    request.argument = user_command.argument; // Doesn't matter if undefined, it will not be read if the command doesn't require it
    char request_buffer[MAX_REQUEST_SIZE];
    error_code = format_request(&request, request_buffer, MAX_REQUEST_SIZE);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while formatting command to send");
        exit(error_code);
    }

    if(write(snd_command_fd, request_buffer, MAX_REQUEST_SIZE) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, MANUAL_INTERACTION_ID, "while sending command to device");
        exit(UNABLE_TO_WRITE_PIPE);
    }

    printf("Command request successfully sent to device %d\n", user_command.target); // TODO: maybe change specifying the device type
    printf("The response will be sent to the Controller\n");

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
        if(argc != 3) {
            return INVALID_COMMAND;
        }
        user_command->code = INFO_COMMAND;
        user_command->message_code = INFO;
        return OK;
    }
    return INVALID_COMMAND;
}

error_code_t parse_switch_command(user_command_t *user_command, int argc, char *argv[]) {
    user_command->code = SWITCH_COMMAND;
    if(argc != 5) {
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
    if(argc != 5) {
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
        int argument = parse_time(argv[4]);
        if(IS_RETURN_ERROR(argument)) {
            return INVALID_COMMAND_ARGUMENT;
        }
        user_command->argument = argument;
        return OK;
    }
    int argument = string_to_unsigned(argv[4]);
    if(IS_RETURN_ERROR(argument)) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(REGISTRY_SUBCOMMAND(message_code) == REGISTRY_DELAY) {
        if(argument < MIN_DELAY || argument > MAX_DELAY) {
            return INVALID_COMMAND_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(message_code) == REGISTRY_THERMOSTAT) {
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

void open_device_pipe(device_id_t target) {
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(target, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (snd_command_fd = open(name, O_WRONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, MANUAL_INTERACTION_ID, "opening the device pipe to send command");
        exit(UNABLE_TO_OPEN_PIPE);
    }
}