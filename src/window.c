#define _XOPEN_SOURCE 700

#include "window.h"

//start - make FILE="window" (ARGS="id" only to test, then it's given by the controller)
//print_error only if i can't send an error response

//response: id command_code response_code arguments

//! switch iff
//make: *** [Makefile:25: default] Segmentation fault (core dumped)

//! info on
//Request: 1 72
//Response: 1 72 0 0 0

//! The comments need to be fixed, now they are just for me, apart the one that are good

// - Explicit device data -

device_id_t id;
leaf_device_state_t state = STATE_CLOSED;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
time_t last_opened; //returns the time in seconds
time_t last_closed;

// - IPC data -

int rcv_requests_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd; //write is non blocking  - pipe to send responses to parent or manual commands
bool force_exit = false;
request_t request; //destination, command_code, argument
response_t response;
char buffer_read[MAX_REQUEST_SIZE]; //buffer to read the request from the pipe
char buffer_write[MAX_RESPONSE_SIZE]; //buffer to write the response before send it to the pipe

// - Signal handler -

struct sigaction action_handler; //to set what to do when a signal occurs

// the controller starts a window process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown; //set the function to be called when the signal occurs
    sigaction(SIGTERM, &action_handler, NULL);

    last_closed = last_opened = time(NULL);
    srand(last_closed); //set random seed with the current time so it's always different

    //the main thread is blocked waiting for requests in a loop (while the exit is not forced by the delete command)
    //when a request is received it is executed, one by one in order of arrival
    while(!force_exit) {
        execute_command();
    }
    handle_shutdown();
}

void handle_shutdown() {
    //it's different from the start_device_fifos but it's not a problem, it's not NULL because the first
    //takes pointers while this it doesn't
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, NO_FILE_DESCRIPTOR);
    if(error_code != OK){
        //prints the error on standard error, best practice to do
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    exit(error_code);
}

error_code_t read_pipe(){
    ssize_t size = read(rcv_requests_fd, buffer_read, MAX_REQUEST_SIZE);

    if(size < 0){
        return UNABLE_TO_READ_PIPE;
    }
    else if(size == 0){
        force_exit = true;
        return UNEXPECTED_END_OF_FILE; //EOF only if it doesn not have a parent anymore
    }
    return parse_request(&request, buffer_read, MAX_REQUEST_SIZE);
}

void write_pipe(){
    error_code_t error_code = format_response(&response, buffer_write, MAX_RESPONSE_SIZE);

    if(error_code != OK){
        print_error(STDERR_FILENO, error_code, id, "while formatting response");
    }
    else if(write(snd_responses_fd, buffer_write, MAX_RESPONSE_SIZE) < 0){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

void execute_command(){
    command_code_t code;
    response.arguments_size = 0;

    error_code_t error_code = read_pipe();
    if(error_code != OK){
        response.command_code = NULL_COMMAND;
        response.response_code = error_code;
    }
    else if(request.destination != id) {
        response.response_code = DESTINATION_ID_MISMATCH;
    }
    else{
        //no errors occurred while parsing the request and the destination is correct
        code = request.command_code;
        response.command_code = code;
        response.response_code = OK;

        if(IS_INFO(code)) { create_info_response(); }
        else if(IS_LINK(code)) { create_link_response(); }
        else if((IS_SWITCH(code))) { create_switch_response(); }
        else if((IS_DELETE(code))) { force_exit=true; } //then the response is sent, the while loop finishes and it shutdowns
        else{
            response.response_code = UNEXPECTED_COMMAND;
        }
    }
    simulate_processing_time();
    write_pipe();
}

void create_info_response(){
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[OPEN_SECONDS_ARGUMENT] = (state==STATE_CLOSED ? LAST_SECONDS_OPEN : CURRENT_SECONDS_OPEN);
    response.arguments_size = MAX_WINDOW_ARGUMENTS;
}

void create_link_response(){
    if(LINK_SUBCOMMAND(request.command_code)==LINK_CHANGE_PARENT){
        //the request argument is the new parent id
        u_int16_t new_parent_id = request.argument;
        response.arguments[REQUEST_ARGUMENT] = new_parent_id; //to give always a feedback
        response.arguments_size = 1;

        if(parent_id!=new_parent_id){
            response.response_code = change_snd_responses_pipe(new_parent_id,&snd_responses_fd);
            if(response.response_code == OK){
                parent_id = new_parent_id;
            }
        }
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}

//i have 2 switch (open,close -> on/off)
//there are 2 combinations that are useless 
void create_switch_response(){
    if(SWITCH_POSITION(request.command_code)==POSITION_ON){
        if(SWITCH_LABEL(request.command_code)==SWITCH_OPEN && state != STATE_OPEN){
            state = STATE_OPEN;
            time(&last_opened);
        }
        else if(SWITCH_LABEL(request.command_code)==SWITCH_CLOSE && state != STATE_CLOSED){
            state = STATE_CLOSED;
            time(&last_closed);
        }
    }
    else if((!(NO_ACTION_SWITCH_CLOSE)) && (!(NO_ACTION_SWITCH_OPEN))){
        response.response_code = UNEXPECTED_COMMAND;
    }
}