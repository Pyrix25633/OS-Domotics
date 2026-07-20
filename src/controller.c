#define _XOPEN_SOURCE 700

#include "controller.h"

#include <errno.h> // ! remove

// - Ncurses data -
bool redirect;
WINDOW *output_win;
WINDOW *input_win;

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely
pthread_t stderr_thread; // Used for reading from redirected `stdout` and `stderr`

// - IPC data -

int rcv_responses_fd;
bool force_exit = false;
char request_buffer[MAX_REQUEST_SIZE];
char response_buffer[MAX_RESPONSE_SIZE];
request_t request;
response_t response;
routing_table_t routing_table;
int stderr_read_fd;
int original_stderr_fd;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler);

    start_responses_fifo();

    init_routing_table(routing_table);

    error_code_t error_code = start_ncurses(argc, argv);
    if(IS_ERROR(error_code)) { // Normal terminal and streams should be available again
        print_error(STDERR_FILENO, error_code, CONTROLLER_ID, "while starting ncurses");
    }

    // Testing
    char buffer[64] = "";
    while(strcmp(buffer, "exit") != 0) {
        input(buffer, 64);
        output("%s", buffer);
    }

    handle_shutdown(OK); // TODO: change passed error code
}

void start_responses_fifo() {
    char name[PIPE_NAME_MAX_LENGTH];
    /*
     The pipe is opened in read write, this way the open doesn't block on startup and the read doesn't get
     end of file every time there is no device in writing mode because the controller is empty
    */
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || mkfifo(name, PIPE_PERMISSIONS) < 0
        || (rcv_responses_fd = open(name, O_RDWR | O_CLOEXEC)) < 0) {
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_PIPE, CONTROLLER_ID, "while creating the pipe to receive responses");
        exit(UNABLE_TO_CREATE_PIPE);
    }
}

error_code_t end_responses_fifo() {
    error_code_t error_code = OK;
    char name[PIPE_NAME_MAX_LENGTH];    
    if(close(rcv_responses_fd) < 0) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || remove(name) < 0) {
        error_code = UNABLE_TO_REMOVE_PIPE;
    }
    return error_code;
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = end_responses_fifo();
    error_code_t tmp;
    if(IS_ERROR(error_code)) {
        print_error(STDERR_FILENO, error_code, CONTROLLER_ID, "while closing and deleting pipes");
    }
    if(redirect) {
        tmp = end_ncurses();
        if(IS_ERROR(tmp)) {
            error_code = tmp;
            print_error(STDERR_FILENO, error_code, CONTROLLER_ID, "while closing ncurses");
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