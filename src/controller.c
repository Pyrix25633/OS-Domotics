#define _XOPEN_SOURCE 700

#include "controller.h"

// - Device data -
const device_id_t id = CONTROLLER_ID;
device_id_t last_device_id = CONTROLLER_ID;
leaf_device_state_t state = STATE_ON;

// - Ncurses data -
bool redirect;
WINDOW *output_win;
WINDOW *input_win;

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely
pthread_mutex_t shutdown_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to ensure shutdown is not called concurrently to avoid remove, close, join an cancel errors
pthread_t stderr_thread; // Used for reading from redirected `stderr`
pthread_t responses_thread; // Used for reading device responses from pipe

// - User input -
user_command_t user_command;
char user_buffer[USER_BUFFER_SIZE];

// - IPC data -

int rcv_responses_fd;
int next_hop_fd;
volatile bool force_exit = false;
volatile bool pending_shutdown = false;
char request_buffer[MAX_REQUEST_SIZE];
char response_buffer[MAX_RESPONSE_SIZE];
request_t request;
response_t response;
routing_table_t routing_table;
int stderr_read_fd;
int original_stderr_fd;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGINT, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler);
    set_signal_handler(SIGCHLD, sigchld_handler);

    start_responses_fifo();

    init_routing_table(routing_table);

    error_code_t error_code = start_ncurses(argc, argv);
    if(IS_ERROR(error_code)) { // Normal terminal and streams should be available again
        print_error(STDERR_FILENO, error_code, id, "while starting ncurses");
    }

    if(pthread_create(&responses_thread, NULL, responses_routine, NULL) != 0) {
        error_code = UNABLE_TO_CREATE_THREAD;
        print_error(STDERR_FILENO, error_code, id, "while creating responses thread");
        force_exit = true; // Fatal error
    }

    output("Controller ID: %d", id);
    output("Available commands:");
    output("- add <type>");
    output("- list");
    output("- sleep <seconds>");
    output("- info <id>");
    output("- switch <id> <label> <position>");
    output("- set <id> <label> <value>");
    output("- link <id1> to <id2>");
    output("- del <id>");

    error_code = execute_scenario();
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, id, "while executing scenario");
    }

    while(!force_exit) {
        input(user_buffer, USER_BUFFER_SIZE);
        error_code = process_user_command(&user_command, user_buffer);
    }

    handle_shutdown(error_code, false);
}

error_code_t process_user_command(user_command_t *user_command, char *string) {
    if(strlen(string) == 0) { // Accept no-op commands, empty lines
        return OK;
    }
    error_code_t error_code = parse_user_command(user_command, string);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, id, "while parsing user command");
        return error_code;
    }
    error_code = check_user_command(user_command);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, id, "while checking user command");
        return error_code;
    }
    error_code = execute_user_command(user_command);
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, id, "while executing user command");
    }
    return error_code;
}

error_code_t parse_user_command(user_command_t *user_command, char *string) {
    char *last;
    char *token = strtok_r(string, " ", &last);
    error_code_t error_code = OK;
    if(token == NULL) {
        return INVALID_COMMAND;
    }
    if(strcmp(token, "add") == 0) {
        error_code = parse_add_command(user_command, &last);
    }
    else if(strcmp(token, "list") == 0) {
        user_command->code = LIST_COMMAND;
    }
    else if(strcmp(token, "sleep") == 0) {
        error_code = parse_sleep_command(user_command, &last);
    }
    else {
        error_code = parse_command_target(user_command, &last);
        if(strcmp(token, "info") == 0) {
            user_command->code = INFO_COMMAND;
            user_command->message_code = INFO;
        }
        else if(strcmp(token, "switch") == 0) {
            if(IS_ERROR(error_code)) {
                return error_code;
            }
            error_code = parse_switch_command(user_command, &last);
        }
        else if(strcmp(token, "set") == 0) {
            if(IS_ERROR(error_code)) {
                return error_code;
            }
            error_code = parse_set_command(user_command, &last);
        }
        else if(strcmp(token, "link") == 0) {
            if(IS_ERROR(error_code)) {
                return error_code;
            }
            error_code = parse_link_command(user_command, &last);
        }
        else if(strcmp(token, "del") == 0) {
            user_command->code = DELETE_COMMAND;
            user_command->message_code = DELETE;
        }
        else {
            error_code = INVALID_COMMAND;
        }
    }
    if(IS_ERROR(error_code)) {
        return error_code;
    }
    return CHECK_NO_OTHER_ARGUMENTS(last);
}

error_code_t parse_add_command(user_command_t *user_command, char **last) {
    char *type = strtok_r(NULL, " ", last);
    if(type == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(strcmp(type, "bulb") == 0) {
        user_command->argument = BULB_DEVICE;
    }
    else if(strcmp(type, "window") == 0) {
        user_command->argument = WINDOW_DEVICE;
    }
    else if(strcmp(type, "fridge") == 0) {
        user_command->argument = FRIDGE_DEVICE;
    }
    else if(strcmp(type, "hub") == 0) {
        user_command->argument = HUB_DEVICE;
    }
    else if(strcmp(type, "timer") == 0) {
        user_command->argument = TIMER_DEVICE;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    user_command->code = ADD_COMMAND;
    return OK;
}

error_code_t parse_sleep_command(user_command_t *user_command, char **last) {
    char *seconds = strtok_r(NULL, " ", last);
    if(seconds == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    int parsed = string_to_unsigned(seconds);
    if(IS_RETURN_ERROR(parsed)) {
        return INVALID_COMMAND_ARGUMENT;
    }
    user_command->code = SLEEP_COMMAND;
    user_command->argument = parsed;
    return OK;
}

error_code_t parse_command_target(user_command_t *user_command, char **last) {
    char *target = strtok_r(NULL, " ", last);
    if(target == NULL) {
        return INVALID_TARGET_ID;
    }
    int parsed = string_to_unsigned(target);
    if(IS_RETURN_ERROR(parsed)) {
        return INVALID_TARGET_ID;
    }
    user_command->target = parsed;
    return OK;
}

error_code_t parse_switch_command(user_command_t *user_command, char **last) {
    user_command->code = SWITCH_COMMAND;
    user_command->message_code = SWITCH;
    char *label = strtok_r(NULL, " ", last);
    if(label == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(strcmp(label, "power") == 0) {
        user_command->message_code |= SWITCH_POWER;
    }
    else if(strcmp(label, "open") == 0) {
        user_command->message_code |= SWITCH_OPEN;
    }
    else if(strcmp(label, "close") == 0) {
        user_command->message_code |= SWITCH_CLOSE;
    }
    else if(strcmp(label, "main") == 0) {
        user_command->code = SWITCH_MAIN_COMMAND;
        user_command->argument = MAIN_OFF;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *position = strtok_r(NULL, " ", last);
    if(position == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(strcmp(position, "on") == 0) {
        if(user_command->code == SWITCH_MAIN_COMMAND) {
            user_command->argument = MAIN_ON;
        }
        user_command->message_code |= POSITION_ON;
    }
    else if(strcmp(position, "off") == 0) {
        user_command->message_code |= POSITION_OFF;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    return OK;
}

error_code_t parse_set_command(user_command_t *user_command, char **last) {
    user_command->code = SET_COMMAND;
    user_command->message_code = REGISTRY;
    char *label = strtok_r(NULL, " ", last);
    if(label == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *value = strtok_r(NULL, " ", last);
    if(value == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    int parsed;
    if(strcmp(label, "delay") == 0) {
        user_command->message_code |= REGISTRY_DELAY;
        parsed = string_to_unsigned(value);
        if(IS_RETURN_ERROR(parsed) || parsed < MIN_DELAY || parsed > MAX_DELAY) {
            return INVALID_COMMAND_ARGUMENT;
        }
    }
    else {
        if(strcmp(label, "begin") == 0) {
        user_command->message_code |= REGISTRY_BEGIN;
        }
        else if(strcmp(label, "end") == 0) {
            user_command->message_code |= REGISTRY_END;
        }
        else {
            return INVALID_COMMAND_ARGUMENT;
        }
        parsed = parse_time(value);
        if(IS_RETURN_ERROR(parsed)) {
            return INVALID_COMMAND_ARGUMENT;
        }
    }
    user_command->argument = parsed;
    return OK;
}

error_code_t parse_link_command(user_command_t *user_command, char **last) {
    char *to = strtok_r(NULL, " ", last);
    if(to == NULL || strcmp(to, "to") != 0) {
        return INVALID_COMMAND;
    }
    char *parent = strtok_r(NULL, " ", last);
    if(parent == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    int parsed = string_to_unsigned(parent);
    if(IS_RETURN_ERROR(parsed)) {
        return INVALID_COMMAND_ARGUMENT;
    }
    user_command->code = LINK_COMMAND;
    user_command->message_code = LINK | LINK_CHANGE_PARENT;
    user_command->argument = parsed;
    return OK;
}

error_code_t check_user_command(user_command_t *user_command) {
    if(user_command->code == SWITCH_MAIN_COMMAND && user_command->target != id) { // Switch main not of Controller
        return DEVICE_TYPE_MISMATCH;
    }
    if(state == STATE_OFF) { // Limit the possible commands to switch main, list, info 0
        switch(user_command->code) {
            case LIST_COMMAND: return OK;
            case DELETE_COMMAND:
            case INFO_COMMAND: if(user_command->target == id) { return OK; } break;
            case SWITCH_MAIN_COMMAND: return OK;
        }
        return SYSTEM_OFF;
    }
    if(!IS_MESSAGE(user_command->code)) { // Nothing else to check
        return OK;
    }
    if((IS_DELETE(user_command->message_code) || IS_INFO(user_command->message_code))&& user_command->target == id) {
        // Delete of everything or info of Controller
        return OK;
    }
    if(user_command->target == id) { // Cannot do other operations on Controller
        return DEVICE_TYPE_MISMATCH;
    }
    // Check that destination exists and eventually that the device type is compatible
    if(pthread_mutex_lock(&data_mutex) != 0) {
        return UNABLE_TO_LOCK_MUTEX;
    }
    error_code_t error_code = OK;
    routing_data_t *target = find_routing_data(routing_table, user_command->target);
    if(target == NULL) { // Also covers messages where the destination is the Controller itself
        error_code = DEVICE_NOT_FOUND;
    }
    else if(IS_SWITCH(user_command->message_code)) { // Checking type compatibility with label
        if(IS_BULB_LIKE(target->type)) {
            if(SWITCH_LABEL(user_command->message_code) != SWITCH_POWER) {
                error_code = DEVICE_TYPE_MISMATCH;
            }
        }
        else if(IS_WINDOW_LIKE(target->type) || IS_FRIDGE_LIKE(target->type)) {
            if(SWITCH_LABEL(user_command->message_code) == SWITCH_POWER) {
                error_code = DEVICE_TYPE_MISMATCH;
            }
        }
        else {
            error_code = DEVICE_TYPE_MISMATCH;
        }
    }
    else if(IS_REGISTRY(user_command->message_code)) { // Checking device type compatibility with registry
        if(IS_TIMER(target->type)) {
            if(REGISTRY_SUBCOMMAND(user_command->message_code) == REGISTRY_DELAY) {
                error_code = DEVICE_TYPE_MISMATCH;
            }
        }
        else if(IS_FRIDGE(target->type)) {
            if(REGISTRY_SUBCOMMAND(user_command->message_code) != REGISTRY_DELAY) {
                error_code = DEVICE_TYPE_MISMATCH;
            }
        }
        else {
            error_code = DEVICE_TYPE_MISMATCH;
        }
    }
    else if(IS_LINK(user_command->message_code)) {
        if(user_command->target == user_command->argument) { // Cannot link to itself
            error_code = LINKING_PARENT_TO_CHILD;
        }
        else if(target->parent_id == user_command->argument) { // The new parent is the old parent, avoid useless requests
            error_code = CANNOT_ADD_TO_PARENT;
        }
        else if(user_command->argument == id) { // Controller is surely type compatible and not one of its children
            error_code = OK;
        }
        else {
            routing_data_t *parent = find_routing_data(routing_table, user_command->argument);
            if(parent == NULL) {
                error_code = DEVICE_NOT_FOUND;
            }
            else if(IS_LEAF(parent->type)) { // Check that the parent is a control device
                error_code = DEVICE_TYPE_MISMATCH;
            }
            else if(IS_TIMER(parent->type) && find_direct_routing_data(routing_table, parent->id, NULL) != NULL) {
                // The parent is a timer it already has a child
                error_code = CANNOT_ADD_TO_PARENT;
            }
            else { // Check that the target is not currently a parent of its future parent (detect cycles) and type compatibility
                while(parent != NULL) { // From here they are surely all control devices
                    if(parent->parent_id == user_command->target) {
                        error_code = LINKING_PARENT_TO_CHILD;
                    }
                    if(!IS_EMPTY(target->type) && !IS_EMPTY(parent->type) && CHILD_TYPE(target->type) != CHILD_TYPE(parent->type)) {
                        error_code = DEVICE_TYPE_MISMATCH;
                    }
                    parent = find_routing_data(routing_table, parent->parent_id);
                }
            }
        }
    }
    if(!IS_ERROR(error_code)) {
        next_hop_fd = target->next_hop_fd;
    }
    if(pthread_mutex_unlock(&data_mutex) != 0) {
        force_exit = true;
        return UNABLE_TO_LOCK_MUTEX;
    }
    return error_code;
}

error_code_t execute_user_command(user_command_t *user_command) {
    if(IS_MESSAGE(user_command->code)) {
        if(user_command->code == INFO_COMMAND && user_command->target == id) {
            routing_data_t *current = find_all_routing_data(routing_table, id, NULL);
            u_int16_t count = 0;
            while(current != NULL) { // Loop all devices
                if(current->parent_id == id) { // Count as direct child
                    count++;
                }
                current = find_all_routing_data(routing_table, id, current);
            }
            output("Controller: state: %s, number of directly connected devices: %u",
                state == STATE_ON ? "on" : "off", count);
            return OK;
        }
        request.command_code = user_command->message_code;
        request.argument = user_command->argument;
        if(user_command->code == DELETE_COMMAND && user_command->target == id) { // Request delete of all direct children
            return execute_delete_command();
        }
        else { // Forward request to destination
            request.destination = user_command->target;
            return write_pipe(&request, request_buffer, MAX_REQUEST_SIZE, next_hop_fd);
        }
    }
    else {
        if(user_command->code == LIST_COMMAND) { // List device information
            execute_list_command();
        }
        else if(user_command->code == SLEEP_COMMAND) { // Sleep specified seconds
            sleep(user_command->argument);
        }
        else if(user_command->code == SWITCH_MAIN_COMMAND) { // Switch main
            if(state != user_command->argument) {
                state = user_command->argument;
                output("Controller: system is now %s", state == STATE_ON ? "on" : "off");
            }
        }
        else { // Add device
            return execute_add_command(user_command->argument);
        }
    }
    return OK;
}

void execute_list_command() {
    routing_data_t *current = find_all_routing_data(routing_table, id, NULL);
    u_int16_t count = 0;
    output("Controller: active devices:");
    while(current != NULL) { // Loop all devices
        if(current->parent_id == id) { // Count as direct child
            count++;
        }
        output_device(current);
        current = find_all_routing_data(routing_table, id, current);
    }
    output("Number of devices directly connected to the Controller: %d", count);
}

error_code_t execute_add_command(device_type_t type) {
    char pipe_name[PIPE_NAME_MAX_LENGTH];
    device_id_t new_id = last_device_id + 1;
    if(IS_ERROR(create_fifo_name(new_id, DIRECTION_DOWN, pipe_name, PIPE_NAME_MAX_LENGTH))
        || mkfifo(pipe_name, PIPE_PERMISSIONS) < 0) {
        return UNABLE_TO_CREATE_PIPE;
    }
    pid_t pid = fork();
    if(pid != 0) { // Controller
        if(pid < 0) {
            return UNABLE_TO_FORK;
        }
        /*
         Pipe is opened in read-write so the open doesn't block, this because the child can
         encounter errors before opening it and keep the Controller blocked
         It is then put in write only with the `O_CLOEXEC` flag so that it's automatically closed on exec
        */
        int fd;
        if((fd = open(pipe_name, O_RDWR)) < 0
            || fcntl(fd, F_SETFD, FD_CLOEXEC)) {
            return UNABLE_TO_OPEN_PIPE;
        }
        if(pthread_mutex_lock(&data_mutex) != 0) {
            return UNABLE_TO_LOCK_MUTEX;
        }
        error_code_t error_code = insert_direct_routing_data_pid(routing_table, new_id, pid, type, id, fd);
        error_code_t tmp = export_routing_table(routing_table, id);
        if(IS_ERROR(tmp)) {
            error_code = tmp;
        }
        if(pthread_mutex_unlock(&data_mutex) != 0) {
            force_exit = true;
            return UNABLE_TO_UNLOCK_MUTEX;
        }
        last_device_id = new_id;
        char device_type[DEVICE_TYPE_SIZE];
        format_device_type(type, device_type);
        output("Successfully created %s with ID: %u, PID: %u", device_type, new_id, pid);
        return error_code;
    }
    else { // New device
        char executable_name[EXECUTABLE_NAME_MAX_LENGTH];
        char full_path[EXECUTABLE_NAME_MAX_LENGTH];
        char id_string[8];
        snprintf(id_string, 8, "%u", new_id);
        create_executable_name(type, full_path, executable_name);
        char *arguments[3]; // Executable name and ID, must be a NULL-terminated array
        arguments[0] = executable_name;
        arguments[1] = id_string;
        arguments[2] = NULL;
        execv(full_path, arguments);
        // If here the exec failed
        print_error(STDERR_FILENO, UNABLE_TO_EXEC, new_id, "after fork");
        exit(UNABLE_TO_EXEC); // Exit is then detected with `SIGCHLD`
    }
}

error_code_t execute_delete_command() {
    if(pthread_mutex_lock(&data_mutex) != 0) {
        return UNABLE_TO_LOCK_MUTEX;
    }
    // Loop all direct children
    routing_data_t *direct = find_direct_routing_data(routing_table, id, NULL);
    error_code_t error_code = OK;
    if(direct == NULL) { // No children, can directly terminate
        force_exit = true;
    }
    pending_shutdown = true;
    while(direct != NULL) {
        request.destination = direct->id;
        error_code = write_pipe(&request, request_buffer, MAX_REQUEST_SIZE, direct->next_hop_fd); // Send delete request to all direct children
        direct = find_direct_routing_data(routing_table, id, direct);
    }
    if(pthread_mutex_unlock(&data_mutex) != 0) {
        force_exit = true;
        return UNABLE_TO_LOCK_MUTEX;
    }
    return error_code;
}

error_code_t execute_scenario() {
    FILE *scenario_file = fopen(SCENARIO_FILE_NAME, "r");
    if(scenario_file == NULL) {
        if(errno == ENOENT) { // File does not exist, just don't execute scenario
            return OK;
        }
        return UNABLE_TO_OPEN_FILE; // Other error
    }

    int last;
    while(fgets(user_buffer, USER_BUFFER_SIZE, scenario_file) != NULL && !force_exit) {
        last = strlen(user_buffer) - 1;
        if(user_buffer[last] == '\n') {
            user_buffer[last] = '\0'; // Remove new line, which not read in normal user input
        }
        process_user_command(&user_command, user_buffer); // Errors are only printed
    }

    if(fclose(scenario_file) != 0) {
        return UNABLE_TO_CLOSE_FILE;
    }
    return OK;
}

void output_device(routing_data_t *device) {
    char type[DEVICE_TYPE_SIZE];
    format_device_type(device->type, type);
    output("%s, ID: %d, Parent ID: %d, PID: %d", type, device->id, device->parent_id, device->pid);
}

void output_response(response_t *response) {
    char user_message[USER_MESSAGE_SIZE];
    char response_status[RESPONSE_STATUS_SIZE];
    char device_type[DEVICE_TYPE_SIZE];
    command_code_t code = response->command_code;
    routing_data_t *source = find_routing_data(routing_table, response->source);
    if(source == NULL) {
        output("Response from unknown device with source ID: %u", response->source);
        return;
    }
    format_device_type(source->type, device_type);
    if(IS_ERROR(response->response_code)) {
        char type[ERROR_TYPE_SIZE];
        char info[ERROR_INFO_SIZE];
        set_error_type_info(response->response_code, type, info);
        snprintf(response_status, RESPONSE_STATUS_SIZE, "failed, %s error: 0x%2x %s", type, response->response_code, info);
    }
    else {
        strcpy(response_status, "successful");
    }
    if(IS_SWITCH(code)) {
        char child_error[CHILD_ERROR_SIZE];
        snprintf(child_error, CHILD_ERROR_SIZE, ", child error 0x%2x", response->arguments[ADDITIONAL_INFO_ARGUMENT]);
        snprintf(user_message, USER_MESSAGE_SIZE, "switch %s %s%s",
            SWITCH_LABEL(code) == SWITCH_POWER ? "power" : (SWITCH_LABEL(code) == SWITCH_OPEN ? "open" : "close"),
            SWITCH_POSITION(code) == POSITION_ON ? "on" : "off",
            response->arguments_size == 1 ? child_error : "");
    }
    else if(IS_INFO(code)) {
        if(IS_ERROR(response->response_code)) {
            strcpy(user_message, "info");
        }
        else {
            format_info_user_message(response, user_message, source->type);
            strcpy(response_status, "");
        }
    }
    else if(IS_DELETE(code)) {
        char child_error[CHILD_ERROR_SIZE];
        snprintf(child_error, CHILD_ERROR_SIZE, ", child error 0x%2x", response->arguments[ADDITIONAL_INFO_ARGUMENT]);
        snprintf(user_message, USER_MESSAGE_SIZE, "delete%s", response->arguments_size == 1 ? child_error : "");
    }
    else if(IS_LINK(code)) {
        if(LINK_SUBCOMMAND(code) == LINK_CHANGE_PARENT) {
            snprintf(user_message, USER_MESSAGE_SIZE, "change parent to %u", response->arguments[PARENT_ID_ARGUMENT]);
        }
        else {
            snprintf(user_message, USER_MESSAGE_SIZE, "remove child %d", response->arguments[CHILD_ID_ARGUMENT]);
        }
    }
    else if(IS_REGISTRY(code)) {
        format_set_user_message(response, user_message);
    }
    else {
        strcpy(user_message, "unknown command");
    }
    output("%s with ID %u: %s %s", device_type, source->id, user_message, response_status);
}

error_code_t update_with_link_response(response_t *response) {
    if(LINK_SUBCOMMAND(response->command_code) == LINK_REMOVE_CHILD) { // Update type
        routing_data_t *device = find_routing_data(routing_table, response->source);
        if(device == NULL) {
            return CHILD_NOT_FOUND;
        }
        if(response->arguments_size != 2) {
            return RESPONSE_FORMAT_ERROR;
        }
        device->type = response->arguments[DEVICE_TYPE_ARGUMENT];
        update_type_to_empty(device);
        return export_routing_table(routing_table, id);
    }
    // Link change parent
    routing_data_t *source = find_routing_data(routing_table, response->source);
    if(source == NULL) {
        return CHILD_NOT_FOUND;
    }
    if(source->parent_id == response->arguments[PARENT_ID_ARGUMENT]) { // Parent didn't change
        return OK;
    }

    if(source->parent_id == id) { // Controller is the old parent
        if(close(source->next_hop_fd) < 0) {
            return UNABLE_TO_CLOSE_PIPE;
        }
        insert_indirect_routing_data_pid(routing_table, source->id, source->pid, source->type, response->arguments[PARENT_ID_ARGUMENT]);
        source = find_routing_data(routing_table, response->source); // Replaced, need to find the new instance
        if(source == NULL) {
            return CHILD_NOT_FOUND;
        }
        update_type_to_not_empty(source);
        /*
         Data about the children of moved device will be updated automatically when the Controller
         receives the "replay history" messages of the moved device, if it is a control device
         */
        return export_routing_table(routing_table, id);
    }

    error_code_t error_code = OK;
    request_t request;
    char request_buffer[MAX_REQUEST_SIZE];
    request.destination = source->parent_id;
    request.command_code = LINK | LINK_REMOVE_CHILD;
    request.argument = source->id;
    
    routing_data_t *old_parent = find_routing_data(routing_table, source->parent_id);
    if(old_parent == NULL) {
        return ROUTE_NOT_FOUND;
    }

    if(response->arguments[PARENT_ID_ARGUMENT] == id) { // Controller is the new parent
        char pipe_name[PIPE_NAME_MAX_LENGTH];
        int fd;
        if(IS_ERROR(create_fifo_name(source->id, DIRECTION_DOWN, pipe_name, PIPE_NAME_MAX_LENGTH))
            || (fd = open(pipe_name, O_WRONLY | O_CLOEXEC)) < 0) { // Shouldn't block because already open by child
            return UNABLE_TO_OPEN_PIPE;
        }
        error_code_t tmp = insert_direct_routing_data_pid(routing_table, source->id, source->pid, source->type, id, fd);
        if(IS_ERROR(tmp)) {
            error_code = tmp;
        }
    }
    else {
        error_code = insert_indirect_routing_data_pid(routing_table, source->id, source->pid, source->type, response->arguments[PARENT_ID_ARGUMENT]);
    }

    source = find_routing_data(routing_table, response->source); // Replaced, need to find the new instance
    if(source == NULL) {
        return CHILD_NOT_FOUND;
    }

    update_type_to_not_empty(source);
    error_code_t tmp = export_routing_table(routing_table, id);
    if(IS_ERROR(tmp)) {
        error_code = tmp;
    }
    
    tmp = write_pipe(&request, request_buffer, MAX_REQUEST_SIZE, old_parent->next_hop_fd);
    return IS_ERROR(tmp) ? tmp : error_code;
}

error_code_t update_with_delete_response(response_t *response) {
    routing_data_t *device = find_routing_data(routing_table, response->source);
    if(device == NULL) {
        return CHILD_NOT_FOUND;
    }
    error_code_t error_code = OK;
    // No need to handle cascading deletion here, it is already handled by Hub and Timer
    if(device->parent_id == id) { // Pipe has to be closed
        if(close(device->next_hop_fd) < 0) {
            error_code = UNABLE_TO_CLOSE_PIPE;
        }
    }
    // Remove pipe
    char pipe_name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(device->id, DIRECTION_DOWN, pipe_name, PIPE_NAME_MAX_LENGTH))
        || (remove(pipe_name) < 0 && errno != ENOENT)) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    routing_data_t *parent = find_routing_data(routing_table, device->parent_id);
    remove_routing_data(routing_table, device->id, device->parent_id);
    if(parent != NULL) {
        update_type_to_empty(parent);
    }
    if(pending_shutdown && find_direct_routing_data(routing_table, id, NULL) == NULL) { // Pending Controller delete and no more children
        force_exit = true;
    }
    error_code_t tmp = export_routing_table(routing_table, id);
    if(IS_ERROR(tmp)) {
        error_code = tmp;
    }
    return error_code;
}

void update_type_to_empty(routing_data_t *device) {
    routing_data_t *current = device;
    while(current != NULL) { // Recalculate types up to topmost parent if necessary
        if(IS_EMPTY(current->type)) {
            return;
        }

        routing_data_t *child = find_direct_routing_data(routing_table, current->id, NULL);
        bool empty = true;
        while(child != NULL) {
            if(!IS_EMPTY(child->type)) {
                empty = false;
                break;
            }
            child = find_direct_routing_data(routing_table, current->id, child);
        }
        if(empty) {
            current->type = IS_HUB(current->type) ? HUB_DEVICE : TIMER_DEVICE;
        }

        current = find_routing_data(routing_table, current->parent_id);
    }
}

void update_type_to_not_empty(routing_data_t *device) {
    device_type_t child_type = device->type;
    if(IS_EMPTY(child_type)) {
        return; // Nothing to update
    }
    child_type &= LEAF_DEVICE_MASK; // Get children type
    routing_data_t *parent = find_routing_data(routing_table, device->parent_id);
    while(parent != NULL) { // Recalculate types up to topmost parent if necessary
        if(!IS_EMPTY(parent->type)) {
            // Already has a type, it shouldn't happen that it is incompatible, anyways nothing could be done
            break;
        }
        // Parent is always a control device, just set children type
        parent->type = (parent->type & CONTROL_DEVICE_MASK) | child_type;
        parent = find_routing_data(routing_table, parent->parent_id);
    }
    return;
}

void format_info_user_message(response_t *response, char user_message[USER_MESSAGE_SIZE], device_type_t type) {
    control_device_state_t state = response->arguments[STATE_ARGUMENT];
    u_int16_t time = response->arguments[OPEN_SECONDS_ARGUMENT]; // Or `ON_SECONDS_ARGUMENT`
    char state_string[16];
    if(state == STATE_MANUAL_OVERRIDE) {
        strcpy(state_string, "manual override");
    }
    else if(state == UNDEFINED_STATE) {
        strcpy(state_string, "undefined");
    }
    else if(IS_BULB_LIKE(type)) {
        if(state == STATE_ON) {
            strcpy(state_string, "on");
        }
        else {
            strcpy(state_string, "off");
        }
    }
    else {
        if(state == STATE_OPEN) {
            strcpy(state_string, "open");
        }
        else {
            strcpy(state_string, "closed");
        }
    }
    if(IS_BULB(type)) {
        snprintf(user_message, USER_MESSAGE_SIZE, "state: %s, last time on: %um", state_string, time/60);
    }
    else if(IS_WINDOW(type)) {
        snprintf(user_message, USER_MESSAGE_SIZE, "state: %s, last time open: %um", state_string, time/60);
    }
    else if(IS_FRIDGE(type)) {
        snprintf(user_message, USER_MESSAGE_SIZE, "state: %s, last time open: %us, delay: %us, percentage: %u%%, thermostat: %u°C, temperature: %.1f°C",
            state_string, time, response->arguments[AUTOCLOSE_DELAY_ARGUMENT], response->arguments[FILL_PERCENTAGE_ARGUMENT],
            response->arguments[THERMOSTAT_ARGUMENT], response->arguments[TEMPERATURE_ARGUMENT]/10.0);
    }
    else {
        char intermediate[RESPONSE_STATUS_SIZE];
        if(IS_BULB_LIKE(type)) {
            snprintf(intermediate, RESPONSE_STATUS_SIZE, "state: %s, max last time on: %um", state_string, time/60);
        }
        else if(IS_WINDOW_LIKE(type)) {
            snprintf(intermediate, RESPONSE_STATUS_SIZE, "state: %s, max last time open: %um", state_string, time/60);
        }
        else if(IS_FRIDGE_LIKE(type)) {
            snprintf(intermediate, RESPONSE_STATUS_SIZE, "state: %s, max last time open: %us", state_string, time);
        }
        else {
            snprintf(intermediate, RESPONSE_STATUS_SIZE, "state: %s", state_string);
        }
        if(IS_HUB(type)) {
            char child_error[CHILD_ERROR_SIZE];
            snprintf(child_error, CHILD_ERROR_SIZE, ", child error 0x%2x", response->arguments[ADDITIONAL_INFO_ARGUMENT]);
            snprintf(user_message, USER_MESSAGE_SIZE, "%s%s", intermediate,
                response->arguments_size == 3 ? child_error : "");
        }
        else {
            char begin[TIME_SIZE];
            char end[TIME_SIZE];
            format_time(begin, response->arguments[BEGIN_ARGUMENT]);
            format_time(end, response->arguments[END_ARGUMENT]);
            snprintf(user_message, USER_MESSAGE_SIZE, "%s, begin: %s, end: %s", intermediate, begin, end);
        }
    }
}

void format_set_user_message(response_t *response, char user_message[USER_MESSAGE_SIZE]) {
    char label_string[16];
    char value_string[16];
    command_code_t code = response->command_code;
    u_int16_t value = response->arguments[REGISTRY_ARGUMENT];
    if(REGISTRY_SUBCOMMAND(code) == REGISTRY_BEGIN) {
        strcpy(label_string, "begin");
        format_time(value_string, value);
    }
    else if(REGISTRY_SUBCOMMAND(code) == REGISTRY_END) {
        strcpy(label_string, "end");
        format_time(value_string, value);
    }
    else if(REGISTRY_SUBCOMMAND(code) == REGISTRY_DELAY) {
        strcpy(label_string, "delay");
        snprintf(value_string, 16, "%us", value);
    }
    else if(REGISTRY_SUBCOMMAND(code) == REGISTRY_PERCENTAGE) {
        strcpy(label_string, "percentage");
        snprintf(value_string, 16, "%u%%", value);
    }
    else if(REGISTRY_SUBCOMMAND(code) == REGISTRY_THERMOSTAT) {
        strcpy(label_string, "thermostat");
        snprintf(value_string, 16, "%u°C", value);
    }
    else {
        strcpy(label_string, "unknown");
        strcpy(value_string, "undefined");
    }
    snprintf(user_message, USER_MESSAGE_SIZE, "set registry %s to %s", label_string, value_string);
}

error_code_t write_pipe(request_t *request, char *request_buffer, size_t size, int fd) {
    error_code_t error_code = format_request(request, request_buffer, size);
    if(IS_ERROR(error_code)) {
        return error_code;
    }
    if(write(fd, request_buffer, size) != (ssize_t)size) {
        return UNABLE_TO_WRITE_PIPE;
    }
    return OK;
}

void* responses_routine(void *arg) {
    (void)arg; // Unused parameter

    int err;
    error_code_t error_code = UNEXPECTED_SHUTDOWN; // Should read at least 1 response if not canceled
    while(!force_exit) {
        error_code = OK;
        err = read(rcv_responses_fd, response_buffer, MAX_RESPONSE_SIZE);
        if(err != MAX_RESPONSE_SIZE) {
            error_code = UNABLE_TO_READ_PIPE;
            print_error(STDERR_FILENO, error_code, id, "in responses thread");
            force_exit = true;
            break; // Fatal error, cannot receive responses from devices
        }
        error_code = parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while parsing response");
            continue;
        }
        if(pthread_mutex_lock(&data_mutex) != 0) {
            error_code = UNABLE_TO_LOCK_MUTEX;
            print_error(STDERR_FILENO, error_code, id, "after receiving response");
            continue;
        }

        if(!IS_ERROR(response.response_code) && IS_LINK(response.command_code)) {
            error_code = update_with_link_response(&response);
        }
        if(!IS_ERROR(error_code)) {
            output_response(&response);
        }
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while parsing response");
        }
        if(!IS_ERROR(response.response_code) && IS_DELETE(response.command_code)) {
            error_code = update_with_delete_response(&response);
        }

        if(pthread_mutex_unlock(&data_mutex) != 0) {
            error_code = UNABLE_TO_UNLOCK_MUTEX;
            print_error(STDERR_FILENO, error_code, id, "after processing response");
            force_exit = true;
        }
    }
    
    // Then this thread makes the entire Controller exit
    handle_shutdown(error_code, true);
    pthread_exit(NULL); // Not really used, just to suppress compiler warning
}

void start_responses_fifo() {
    char name[PIPE_NAME_MAX_LENGTH];
    /*
     The pipe is opened in read write, this way the open doesn't block on startup and the read doesn't get
     end of file every time there is no device in writing mode because the controller is empty
    */
    if(IS_ERROR(create_fifo_name(id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || mkfifo(name, PIPE_PERMISSIONS) < 0
        || (rcv_responses_fd = open(name, O_RDWR | O_CLOEXEC)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, id, "while creating the pipe to receive responses");
        exit(UNABLE_TO_CREATE_PIPE);
    }
}

error_code_t end_responses_fifo() {
    error_code_t error_code = OK;
    char name[PIPE_NAME_MAX_LENGTH];
    // Can happen that handle_shutdown is called concurrently and that the pipe is already closed and removed
    if(close(rcv_responses_fd) < 0) {
       return OK;
    }
    if(IS_ERROR(create_fifo_name(id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || remove(name) < 0) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    return error_code;
}

error_code_t end_child_device_fifos(routing_data_t *device) {
    error_code_t error_code;
    char pipe_name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(device->id, DIRECTION_DOWN, pipe_name, PIPE_NAME_MAX_LENGTH))
        || (remove(pipe_name) < 0 && errno != ENOENT)) {
        // Could not remove, not because the file doesn't exist
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    if(IS_CONTROL(device->type)) {
        if(IS_ERROR(create_fifo_name(device->id, DIRECTION_UP, pipe_name, PIPE_NAME_MAX_LENGTH))
            || (remove(pipe_name) < 0 && errno != ENOENT)) {
            // Could not remove, not because the file doesn't exist
            error_code = UNABLE_TO_REMOVE_PIPE;
        }
    }
    return error_code;
}

void handle_shutdown(error_code_t error, bool in_responses_thread) {
    error_code_t error_code = OK;
    if(pthread_mutex_lock(&shutdown_mutex) != 0) {
        error_code = UNABLE_TO_LOCK_MUTEX;
        print_error(STDERR_FILENO, error_code, id, "while acquiring shutdown mutex");
    }
    error_code_t tmp = end_responses_fifo();
    if(IS_ERROR(tmp)) {
        error_code = tmp;
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    if(redirect) {
        tmp = end_ncurses();
        if(IS_ERROR(tmp)) {
            error_code = tmp;
            print_error(STDERR_FILENO, error_code, id, "while closing ncurses");
        }
    }
    if(!in_responses_thread) {
        tmp = OK;
        if(pthread_cancel(responses_thread) != 0) {
            tmp = UNABLE_TO_CANCEL_THREAD;
        }
        if(pthread_join(responses_thread, NULL) != 0) {
            tmp = UNABLE_TO_JOIN_THREAD;
        }
        if(IS_ERROR(tmp)) {
            error_code = tmp;
            print_error(STDERR_FILENO, error_code, id, "while canceling responses thread");
        }
    }
    tmp = shutdown_devices(id);
    if(IS_ERROR(tmp)) {
        error_code = tmp;
        print_error(STDERR_FILENO, error_code, id, "while cleaning up devices");
    }
    if((remove(REGISTRY_FILE) < 0 || remove(TMP_REGISTRY_FILE) < 0) && errno != ENOENT) {
        error_code = UNABLE_TO_REMOVE_FILE;
        print_error(STDERR_FILENO, error_code, id, "while cleaning up registry files");
    }
    if(!IS_ERROR(error_code)) {
        error_code = error;
    }
    exit(error_code);
}

error_code_t shutdown_devices(device_id_t parent_id) {
    // About to shutdown, mutex not used to read data as it may be locked and/or not working
    routing_data_t *device = find_all_routing_data(routing_table, parent_id, NULL);
    error_code_t error_code = OK;
    error_code_t tmp;
    while(device != NULL) {
        if(kill(device->pid, SIGTERM) < 0) {
            error_code = UNABLE_TO_SEND_SIGNAL;
        }

        if(device->parent_id == id) {
            close(device->next_hop_fd); // Not checking for errors, could be already closed
        }
        tmp = end_child_device_fifos(device);
        if(IS_ERROR(tmp)) {
            error_code = tmp;
        }

        device = find_all_routing_data(routing_table, parent_id, device);
    }
    device_id_t device_id;
    device = find_direct_routing_data(routing_table, parent_id, NULL);
    // Cannot remove Controller from the routing table as it is not present, so all direct children are removed
    while(device != NULL) {
        device_id = device->id;
        device = find_direct_routing_data(routing_table, parent_id, device); // First find next
        remove_routing_data(routing_table, device_id, parent_id); // Then remove current
    }
    return error_code;
}

void sigterm_handler(int sig_num) {
    (void)sig_num; // Unused parameter
    handle_shutdown(UNEXPECTED_SHUTDOWN, false);
}

void sigpipe_handler(int sig_num) {
    (void)sig_num; // Unused parameter
    handle_shutdown(BROKEN_PIPE, false);
}

void sigchld_handler(int sig_num) {
    (void)sig_num; // Unused parameter
    pthread_t sigchld_thread;
    pthread_attr_t sigchld_attributes;
    if(pthread_attr_init(&sigchld_attributes) != 0
        || pthread_attr_setdetachstate(&sigchld_attributes, PTHREAD_CREATE_DETACHED) != 0
        || pthread_create(&sigchld_thread, &sigchld_attributes, sigchld_routine, NULL) != 0) {
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while creating thread to handle SIGCHLD");
    }
}

void* sigchld_routine(void *arg) {
    (void)arg; // Unused parameter

    if(pthread_mutex_lock(&data_mutex) != 0) {
        print_error(STDERR_FILENO, UNABLE_TO_LOCK_MUTEX, id, "acquiring mutex to handle SIGCHLD");
        pthread_exit(NULL);
    }
    // Determine which child/children caused the signal and cleanup
    routing_data_t *current = find_all_routing_data(routing_table, id, NULL);
    int exit_code;
    int status;
    device_id_t current_id;
    device_id_t current_parent_id;
    bool remove;
    error_code_t error_code = OK;
    error_code_t tmp;
    request_t request;
    request.command_code = LINK | LINK_REMOVE_CHILD;
    char request_buffer[MAX_REQUEST_SIZE];
    /*
     It's no longer possible to send requests and receive responses to the children of the dead device/s
     So they are terminated using `SIGTERM`
    */
    while(current != NULL) {
        /*
         When a child process exists with OK it is not removed from the routing information, as that is handled
         by it's delete response through pipes
         It can happen that when receiving multiple SIGCHLD a child that exited successfully, and so is not removed,
         is waited for multiple times, from the second time on it returns with errno ECHILD, it is expected 
        */
        remove = false;
        if((status = waitpid(current->pid, &exit_code, WNOHANG)) < 0) {
            if(errno != ECHILD) { // Unexpected error, meanwhile ECHILD is possible as mentioned above
                print_error(STDERR_FILENO, UNABLE_TO_WAIT, id, "while checking child process status");
            }
        }
        else if(status > 0 && ((WIFEXITED(exit_code) && IS_ERROR(WEXITSTATUS(exit_code))) || WIFSIGNALED(exit_code))) {
            char device_type[DEVICE_TYPE_SIZE];
            format_device_type(current->type, device_type);
            output("%s with ID %d exited with error code: 0x%2x", device_type, current->id, WEXITSTATUS(status));
            /*
             Terminated with error, handle termination of children
             Send notification to parent, to avoid it crashing with SIGPIPE when writing on pipe with no readers
            */
            request.destination = current->parent_id;
            write_pipe(&request, request_buffer, MAX_REQUEST_SIZE, current->next_hop_fd);
            // Remove pipes and shutdown children
            tmp = end_child_device_fifos(current);
            if(IS_ERROR(tmp)) {
                error_code = tmp;
                print_error(STDERR_FILENO, error_code, id, "while cleaning pipes of dead device");
            }
            tmp = shutdown_devices(current->id);
            if(IS_ERROR(tmp)) {
                error_code = tmp;
                print_error(STDERR_FILENO, error_code, id, "while terminating dead device children");
            }
            // Remove data
            remove = true;
        }
        current_id = current->id;
        current_parent_id = current->parent_id;
        current = find_all_routing_data(routing_table, id, current);
        if(remove) {
            output("Removing %u", current_id); // ! remove
            remove_routing_data(routing_table, current_id, current_parent_id);
            tmp = export_routing_table(routing_table, id);
            if(IS_ERROR(tmp)) {
                error_code = tmp;
                print_error(STDERR_FILENO, error_code, id, "while exporting routing table after dead device");
            }
        }
    }
    if(pthread_mutex_unlock(&data_mutex) != 0) {
        error_code = UNABLE_TO_UNLOCK_MUTEX;
        print_error(STDERR_FILENO, error_code, id, "releasing mutex to handle SIGCHLD");
        handle_shutdown(error_code, false);
    }

    /*
     It can happen that some child processes are signaled, so it should exit here
    */
    if(pending_shutdown && find_direct_routing_data(routing_table, id, NULL) == NULL) {
        handle_shutdown(error_code, false);
    }
    pthread_exit(NULL);
}

error_code_t start_ncurses(int argc, char *argv[]) {
    if(argc == 1) { // Implicitly redirect
        redirect = true;
    }
    else if(argc == 2 && strcmp(argv[1], "--no-ncurses") == 0) { // Use standard terminal
        return OK;
    }
    else {
        return INVALID_COMMAND_ARGUMENT;
    }

    initscr();

    error_code_t error_code = redirect_stderr();
    if(IS_ERROR(error_code)) { // Restore original streams
        error_code_t error = restore_stderr();
        redirect = false;
        endwin();
        return IS_ERROR(error) ? error : error_code;
    }

    error_code = create_windows();

    if(pthread_create(&stderr_thread, NULL, stderr_routine, NULL) != 0) {
        return UNABLE_TO_CREATE_THREAD;
    }
    return error_code;
}

error_code_t redirect_stderr() {
    /*
     Pipe is opened in read-write, otherwise the open would block until there's an open in write mode
     It is also created with O_CLOEXEC so that it is automatically closed by `exec`
    */
    if(mkfifo(STDERR_PIPE_NAME, PIPE_PERMISSIONS) < 0
        || (stderr_read_fd = open(STDERR_PIPE_NAME, O_RDWR | O_CLOEXEC)) < 0) {
        return UNABLE_TO_CREATE_PIPE;
    }
    // Then it is also opened in write and used to replace original `stderr`
    original_stderr_fd = dup(STDERR_FILENO);
    int stderr_write_fd = open(STDERR_PIPE_NAME, O_WRONLY);
    if(stderr_write_fd < 0
        || dup2(stderr_write_fd, STDERR_FILENO) < 0) {
        return UNABLE_TO_OPEN_PIPE;
    }
    return OK;
}

error_code_t restore_stderr() {
    if(dup2(original_stderr_fd, STDERR_FILENO) < 0) {
        exit(UNABLE_TO_RESTORE_STDERR);
    }
    if(remove(STDERR_PIPE_NAME) < 0) {
        return UNABLE_TO_REMOVE_PIPE;
    }
    return OK;
}

error_code_t create_windows() {
    int height, width;
    getmaxyx(stdscr, height, width);
    height -= 2; // 2 lines for the window titles
    int output_height = height * 2 / 3;
    int input_height = height - output_height;
    // Lines and titles
    mvwhline(stdscr, 0, 0, 0, width);
    mvwhline(stdscr, output_height + 1, 0, 0, width);
    mvwprintw(stdscr, 0, 2, "Responses");
    mvwprintw(stdscr, output_height + 1, 2, "Commands");
    // Create the windows
    output_win = newwin(output_height, width, 1, 0);
    input_win = newwin(input_height, width, output_height + 2, 0);
    if(output_win == NULL || input_win == NULL) {
        restore_stderr();
        endwin(); // Attempt to restore normal terminal mode
        redirect = false;
        return UNABLE_TO_CREATE_WINDOWS;
    }
    // Set initial position and refresh
    wmove(output_win, output_height - 1, 0);
    wmove(input_win, input_height - 1, 0);
    scrollok(output_win, true);
    scrollok(input_win, true);
    refresh();
    wrefresh(input_win);
    wrefresh(input_win);
    return OK;
}

void* stderr_routine(void *arg) {
    (void)arg; // Unused parameter

    char buffer[STDERR_BUFFER_SIZE];
    FILE *stderr_file = fdopen(stderr_read_fd, "r");
    while(fgets(buffer, STDERR_BUFFER_SIZE, stderr_file) != NULL) {
        buffer[strlen(buffer) - 1] = '\0'; // remove new line automatically inserted while printing
        output("%s", buffer);
    }
    
    pthread_exit(NULL);
}

int output(char *format, ...) {
    va_list args;
    va_start(args, format);
    int n;
    
    if(redirect) {
        n = wprintw(output_win, "\n");
        n += vw_printw(output_win, format, args);
        wrefresh(output_win);
        wrefresh(input_win);
    }
    else {
        n = vprintf(format, args);
        n += printf("\n");
    }

    va_end(args);
    return n;
}

int input(char *buffer, size_t size) {
    if(redirect) {
        return wgetnstr(input_win, buffer, size);
    }
    else {
        if(fgets(buffer, size, stdin) == NULL) {
            return 0;
        }
        int n = strnlen(buffer, size);
        if(n > 0) {
            buffer[--n] = '\0'; // Remove '\n'
        }
        return n;
    }
}

error_code_t end_ncurses() {
    error_code_t error_code = OK;

    if((close(stderr_read_fd) < 0 || close(STDERR_FILENO) < 0)) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    /*
     The thread is canceled because it is very likely blocked on a read call
     It's not a problem since it has nothing else to do and doesn't modify data
     Since the handle shutdown function can be called multiple times this function
     might also be called multiple times, so cancel and join ignore some errors
    */
    if(pthread_cancel(stderr_thread) != 0) {
        error_code = UNABLE_TO_CANCEL_THREAD;
    }
    if(pthread_join(stderr_thread, NULL) != 0) {
        error_code = UNABLE_TO_JOIN_THREAD;
    }

    error_code_t error = restore_stderr();
    if(IS_ERROR(error)) {
        error_code = error;
    }

    endwin();

    return error_code;
}