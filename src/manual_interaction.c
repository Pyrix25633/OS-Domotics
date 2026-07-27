#define _XOPEN_SOURCE 700

#include "manual_interaction.h"

// - User data -

user_command_t user_command;


// - IPC data -

request_t request;
char request_buffer[MAX_REQUEST_SIZE];
int snd_command_fd;

int main(int argc, char *argv[]) {
    // Parse user command
    error_code_t error_code = parse_user_command(&user_command, argc, argv);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while parsing command");
        exit(error_code);
    }

    // Check device type and command

    device_type_t type;
    error_code = find_device_type(user_command.target, &type);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while parsing command");
        exit(error_code);
    }

    error_code = check_user_command(&user_command, type);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while parsing command");
        exit(error_code);
    }

    // Send command request to target device

    open_device_pipe(user_command.target);
    
    request.destination = user_command.target;
    request.command_code = user_command.message_code;
    request.argument = user_command.argument; // Doesn't matter if undefined, it will not be read if the command doesn't require it
    error_code = format_request(&request, request_buffer, MAX_REQUEST_SIZE);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, MANUAL_INTERACTION_ID, "while formatting command to send");
        exit(error_code);
    }

    if(write(snd_command_fd, request_buffer, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE) {
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, MANUAL_INTERACTION_ID, "while sending command to device");
        exit(UNABLE_TO_WRITE_PIPE);
    }

    char device_type[DEVICE_TYPE_SIZE];
    format_device_type(type, device_type);
    printf("Command request successfully sent to %s with ID %u\n", device_type, user_command.target);
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

error_code_t check_user_command(user_command_t *user_command, device_type_t type) {
    if(user_command->code == INFO_COMMAND) { // Nothing to check
        return OK;
    }
    if(IS_SWITCH(user_command->message_code)) {
        if(IS_CONTROL(type) && IS_EMPTY(type)) {
            return DEVICE_TYPE_MISMATCH; // No valid switch operation
        }
        if(SWITCH_LABEL(user_command->message_code) == SWITCH_POWER) {
            return IS_BULB_LIKE(type) ? OK : DEVICE_TYPE_MISMATCH;
        }
        else { // Open or close
            return IS_BULB_LIKE(type) ? DEVICE_TYPE_MISMATCH : OK;
        }
    }
    // Registry set
    if(REGISTRY_SUBCOMMAND(user_command->message_code) == REGISTRY_BEGIN
        || REGISTRY_SUBCOMMAND(user_command->message_code) == REGISTRY_END) {
        return IS_TIMER(type) ? OK : DEVICE_TYPE_MISMATCH;
    }
    // Set delay, thermostat or fill percentage
    return IS_FRIDGE(type) ? OK : DEVICE_TYPE_MISMATCH;
}

void open_device_pipe(device_id_t target) {
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(target, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (snd_command_fd = open(name, O_WRONLY)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, MANUAL_INTERACTION_ID, "opening the device pipe to send command");
        exit(UNABLE_TO_OPEN_PIPE);
    }
}