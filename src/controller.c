#define _XOPEN_SOURCE 700

#include "controller.h"

// - Ncurses data -
bool redirect;
WINDOW *output_win;
WINDOW *input_win;

// - Concurrency management data -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; // Used to access and modify device data safely
pthread_t read_redirected_thread; // Used for reading from redirected `stdout` and `stderr`

// - IPC data -

int rcv_responses_fd;
bool force_exit = false;
char request_buffer[MAX_REQUEST_SIZE];
char response_buffer[MAX_RESPONSE_SIZE];
request_t request;
response_t response;
routing_table_t routing_table;
int stdouterr_read_fd;
int original_stdout_fd;
int original_stderr_fd;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);

    start_responses_fifo();

    init_routing_table(routing_table);

    // ! fails somewhere

    error_code_t error_code = start_ncurses(argc, argv);
    if(IS_ERROR(error_code)) { // Normal terminal and streams should be available again
        print_error(STDERR_FILENO, error_code, CONTROLLER_ID, "while starting ncurses");
    }

    // Testing
    char buffer[64];
    wgetnstr(input_win, buffer, 64);
    printf("%s\n", buffer);
    wgetnstr(input_win, buffer, 64);

    handle_shutdown(OK); // TODO: change passed error code
}

void start_responses_fifo() {
    char name[PIPE_NAME_MAX_LENGTH];
    // The pipe is opened in non-blocking and then restored to blocking, because there is no child that will read yet
    // TODO: most probably it will have to be reopened every time there are no children
    // TODO: check if it can be done differently
    if(IS_ERROR(create_fifo_name(CONTROLLER_ID, DIRECTION_UP, name, PIPE_NAME_MAX_LENGTH))
        || mkfifo(name, PIPE_PERMISSIONS) < 0
        || (rcv_responses_fd = open(name, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0
        || fcntl(rcv_responses_fd, F_SETFD, fcntl(rcv_responses_fd, F_GETFD) & ~O_NONBLOCK) < 0) {
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
    // TODO: probably send del to all children
    if(!IS_ERROR(error_code)) {
        error_code = error;
    }
    exit(error_code);
}

void sigterm_handler() {
    handle_shutdown(UNEXPECTED_SHUTDOWN);
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

    error_code_t error_code = redirect_stdout_stderr();
    if(IS_ERROR(error_code)) { // Restore original streams
        restore_stdout_stderr();
        redirect = false;
        return error_code;
    }

    initscr();
    cbreak(); // TODO: check if needed

    error_code = create_windows();

    if(pthread_create(&read_redirected_thread, NULL, read_redirected_routine, NULL) < 0) { // TODO: currently is an attached thread, check how to make it exit
        return UNABLE_TO_CREATE_THREAD;
    }
    return error_code;
}

error_code_t redirect_stdout_stderr() {
    /*
     Pipe is opened in non-blocking, otherwise the open would block until there's an open in write mode
     It is also created with O_CLOEXEC so that it is automatically closed by `exec`
    */
    if(mkfifo(STDOUTERR_PIPE_NAME, PIPE_PERMISSIONS) < 0
        || (stdouterr_read_fd = open(STDOUTERR_PIPE_NAME, O_RDONLY | O_CLOEXEC | O_NONBLOCK)) < 0) {
        return UNABLE_TO_CREATE_PIPE;
    }
    // Then it is set back to blocking mode, otherwise reads wouldn't block
    if(fcntl(stdouterr_read_fd, F_SETFD, fcntl(stdouterr_read_fd, F_GETFD) & ~O_NONBLOCK) < 0) {
        return UNABLE_TO_SET_FD_ATTR;
    }
    // Then it is also opened in write and used to replace original `stdout` and `stderr`
    original_stdout_fd = dup(STDOUT_FILENO);
    original_stderr_fd = dup(STDERR_FILENO);
    int stdouterr_write_fd = open(STDOUTERR_PIPE_NAME, O_WRONLY);
    if(stdouterr_write_fd < 0
        || dup2(stdouterr_write_fd, STDOUT_FILENO) < 0
        || dup2(stdouterr_write_fd, STDERR_FILENO) < 0) {
        return UNABLE_TO_OPEN_PIPE;
    }
    return OK;
}

error_code_t restore_stdout_stderr() {
    if(dup2(original_stdout_fd, STDOUT_FILENO) < 0
        || dup2(original_stderr_fd, STDERR_FILENO) < 0) {
        exit(UNABLE_TO_RESTORE_STREAMS);
    }
    if(remove(STDOUTERR_PIPE_NAME) < 0) {
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
        restore_stdout_stderr();
        endwin(); // Attempt to restore normal terminal mode
        redirect = false;
        return UNABLE_TO_CREATE_WINDOWS;
    }
    // Set initial position and refresh
    wmove(input_win, output_height - 1, 0);
    wmove(input_win, input_height - 1, 0);
    scrollok(input_win, true);
    scrollok(input_win, true);
    refresh();
    wrefresh(input_win);
    wrefresh(input_win);
    return OK;
}

void* read_redirected_routine(void *arg) {
    (void)arg; // Unused parameter

    char buffer[STDOUTERR_BUFFER_SIZE];
    char tmp;
    int i = 0;
    while((read(stdouterr_read_fd, &tmp, 1)) > 0) { // TODO: maybe change to use `fread`
        if(tmp == '\n' || tmp == '\0' || i == STDOUTERR_BUFFER_SIZE - 1) {
            buffer[i] = '\0';
            wprintw(output_win, "\n%s", buffer);     
            wrefresh(output_win);
            wrefresh(input_win);
            i = 0;
        }
        else {
            buffer[i] = tmp;
            i++;
        }
    }
    
    pthread_exit(NULL);
}

error_code_t end_ncurses() {
    error_code_t error_code = OK;

    if(close(stdouterr_read_fd) < 0 || close(STDOUT_FILENO) < 0 || close(STDERR_FILENO)) {
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    if(pthread_join(read_redirected_thread, NULL) < 0) {
        error_code = UNABLE_TO_JOIN_THREAD;
    }

    endwin();

    return error_code;
}