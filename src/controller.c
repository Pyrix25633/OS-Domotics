#define _XOPEN_SOURCE 700

#include "controller.h"

// - Device data -
device_id_t id = CONTROLLER_ID;
device_id_t last_device_id = CONTROLLER_ID + 1;

// - Ncurses data -
bool redirect;
WINDOW *output_win;
WINDOW *input_win;

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely // ! check if used
pthread_t stderr_thread; // Used for reading from redirected `stderr`

// - User input -
user_command_t user_command;
char user_buffer[USER_BUFFER_SIZE];

// - IPC data -

int rcv_responses_fd;
int next_hop_fd;
volatile bool force_exit = false;
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
    // ! must set `SIGCHLD` handler, must access routing data, so must be run in a different thread, detached probably

    start_responses_fifo();

    init_routing_table(routing_table);

    error_code_t error_code = start_ncurses(argc, argv);
    if(IS_ERROR(error_code)) { // Normal terminal and streams should be available again
        print_error(STDERR_FILENO, error_code, id, "while starting ncurses");
    }

    output("Controller ID: %d", id);
    output("Available commands:");
    output("- add <type>");
    output("- list");
    output("- info <id>");
    output("- switch <id> <label> <position>");
    output("- set <id> <label> <value>");
    output("- link <id1> to <id2>");
    output("- del <id>");

    while(!force_exit) {
        input(user_buffer, USER_BUFFER_SIZE);
        error_code = parse_user_command(&user_command, user_buffer);
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while parsing user command");
            continue;
        }
        error_code = check_user_command(&user_command);
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while parsing user command");
            continue;
        }
        error_code = execute_user_command(&user_command);
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while parsing user command");
            continue;
        }
        // TODO: check that it actually works, need to create the responses thread first
        output("Command code: %d, target: %d, argument: %d", user_command.code, user_command.target, user_command.argument);
    }

    handle_shutdown(OK); // TODO: change passed error code
}

error_code_t parse_user_command(user_command_t *user_command, char *string) {
    char *last;
    char *token = strtok_r(string, " ", &last);
    error_code_t error_code;
    if(token == NULL) {
        return INVALID_COMMAND;
    }
    if(strcmp(token, "add") == 0) {
        error_code = parse_add_command(user_command, &last);
    }
    else if(strcmp(token, "list") == 0) {
        user_command->code = LIST_COMMAND;
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
    else {
        return INVALID_COMMAND_ARGUMENT;
    }
    char *position = strtok_r(NULL, " ", last);
    if(position == NULL) {
        return INVALID_COMMAND_ARGUMENT;
    }
    if(strcmp(position, "on") == 0) {
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
    if(!IS_MESSAGE(user_command->code)) { // Nothing else to check
        return OK;
    }
    if(IS_DELETE(user_command->message_code) && user_command->target == CONTROLLER_ID) { // Delete of everything
        return OK;
    }
    // Check that destination exists and eventually that the device type is compatible
    if(pthread_mutex_lock(&data_mutex) < 0) {
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
        if(user_command->argument == CONTROLLER_ID) { // Controller is surely type compatible and not one of its children
            error_code = OK;
        }
        routing_data_t *parent = find_routing_data(routing_table, user_command->argument);
        if(parent == NULL) {
            error_code = DEVICE_NOT_FOUND;
        }
        if(IS_LEAF(parent->type)) { // Check that the parent is a control device
            error_code = DEVICE_TYPE_MISMATCH;
        }
        // Check that the target is not currently a parent of its future parent (detect cycles) and type compatibility
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
    next_hop_fd = target->next_hop_fd;
    if(pthread_mutex_unlock(&data_mutex) < 0) {
        force_exit = true;
        return UNABLE_TO_LOCK_MUTEX;
    }
    return error_code;
}

error_code_t execute_user_command(user_command_t *user_command) {
    if(IS_MESSAGE(user_command->code)) {
        request.command_code = user_command->message_code;
        request.argument = user_command->argument;
        if(user_command->code == DELETE_COMMAND && user_command->target == CONTROLLER_ID) { // Request delete of all direct children
            return execute_delete_command();
        }
        else { // Forward request to destination
            request.destination = user_command->target;
            return write_pipe();
        }
    }
    else {
        if(user_command->code == LIST_COMMAND) { // List device information
            execute_list_command();
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
    output("Active devices:");
    while(current != NULL) { // Loop all devices
        if(current->parent_id == CONTROLLER_ID) { // Count as direct child
            count++;
        }
        output_device(current);
        current = find_all_routing_data(routing_table, id, current);
    }
    output("Total number of devices directly connected to the Controller: %d", count);
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
        int fd = open(pipe_name, O_WRONLY); // Blocks until the device opens it in read mode
        if(fd < 0) {
            return UNABLE_TO_OPEN_PIPE;
        }
        if(pthread_mutex_lock(&data_mutex) < 0) {
            return UNABLE_TO_LOCK_MUTEX;
        }
        error_code_t error_code = insert_direct_routing_data_pid(routing_table, new_id, pid, type, id, fd);
        if(pthread_mutex_unlock(&data_mutex) < 0) {
            force_exit = true;
            return UNABLE_TO_UNLOCK_MUTEX;
        }
        last_device_id = new_id;
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
        exit(UNABLE_TO_EXEC);
    }
}

error_code_t execute_delete_command() {
    if(pthread_mutex_lock(&data_mutex) < 0) {
        return UNABLE_TO_LOCK_MUTEX;
    }
    // Loop all direct children
    routing_data_t *direct = find_direct_routing_data(routing_table, id, NULL);
    error_code_t error_code;
    while(direct != NULL) {
        request.destination = direct->id;
        next_hop_fd = direct->next_hop_fd;
        error_code = write_pipe(); // Send delete request to all direct children
        direct = find_direct_routing_data(routing_table, id, direct);
    }
    if(pthread_mutex_unlock(&data_mutex) < 0) {
        force_exit = true;
        return UNABLE_TO_LOCK_MUTEX;
    }
    return error_code;
}

void output_device(routing_data_t *device) {
    char type[DEVICE_TYPE_SIZE];
    format_device_type(device->type, type);
    output("%s, ID: %d, Parent ID: %d, PID: %d", type, device->id, device->parent_id, device->pid);
}

error_code_t write_pipe() {
    error_code_t error_code = format_request(&request, request_buffer, MAX_REQUEST_SIZE);
    if(IS_ERROR(error_code)) {
        return error_code;
    }
    if(write(next_hop_fd, request_buffer, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE) {
        return UNABLE_TO_WRITE_PIPE;
    }
    return OK;
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
    if(close(rcv_responses_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(IS_ERROR(create_fifo_name(id, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || remove(name) < 0) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    return error_code;
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = end_responses_fifo();
    error_code_t tmp;
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    if(redirect) {
        tmp = end_ncurses();
        if(IS_ERROR(tmp)) {
            error_code = tmp;
            print_error(STDERR_FILENO, error_code, id, "while closing ncurses");
        }
    }
    // TODO: send `SIGTERM` to all devices if any
    if(!IS_ERROR(error_code)) {
        error_code = error;
    }
    exit(error_code);
}

void sigterm_handler() {
    handle_shutdown(UNEXPECTED_SHUTDOWN);
}

void sigpipe_handler() {
    handle_shutdown(BROKEN_PIPE);
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
    cbreak(); // TODO: check if needed

    error_code_t error_code = redirect_stderr();
    if(IS_ERROR(error_code)) { // Restore original streams
        error_code_t error = restore_stderr();
        redirect = false;
        endwin();
        return IS_ERROR(error) ? error : error_code;
    }

    error_code = create_windows();

    if(pthread_create(&stderr_thread, NULL, stderr_routine, NULL) < 0) { // TODO: currently is an attached thread, check how to make it exit
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
    int output_height = height / 2;
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

    int err;

    char buffer[STDERR_BUFFER_SIZE];
    char tmp;
    int i = 0;
    while((err = read(stderr_read_fd, &tmp, 1)) > 0) { // TODO: maybe change to use `fread`
        if(tmp == '\n' || tmp == '\0' || i == STDERR_BUFFER_SIZE - 1) { // TODO: fix lost char
            buffer[i] = '\0';
            output("%s", buffer);
            i = 0;
        }
        else {
            buffer[i] = tmp;
            i++;
        }
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
        fgets(buffer, size, stdin);
        int n = strnlen(buffer, size);
        if(n > 0) {
            buffer[--n] = '\0'; // Remove '\n'
        }
        return n;
    }
}

error_code_t end_ncurses() {
    error_code_t error_code = OK;

    if(close(stderr_read_fd) < 0 || close(STDERR_FILENO)) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    /*
     The thread is canceled because it is very likely blocked on a read call
     It's not a problem since it has nothing else to do and doesn't modify data
    */
    if(pthread_cancel(stderr_thread) < 0) {
        error_code = UNABLE_TO_JOIN_THREAD;
    }

    error_code_t error = restore_stderr();
    if(IS_ERROR(error)) {
        error_code = error;
    }

    endwin();

    return error_code;
}