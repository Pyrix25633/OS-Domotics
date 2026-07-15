#define _XOPEN_SOURCE 700

#include "window.h"

//start - make FILE="window"
//print_error only if i can't send an error response

// - Explicit device data -

device_id_t id;
leaf_device_state_t state = STATE_CLOSED;
u_int32_t seconds_open = INITIAL_SECONDS_OPEN;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;

// - Timestamps - needed to calculate the open time

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
    response.source = id; //! response source
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown; //set the function to be called when the signal occurs
    sigaction(SIGTERM, &action_handler, NULL);

    //the main thread is blocked waiting for requests in a loop (while the exit is not forced)
    //when a request is received it is executed, one by one in order of arrival
    while(!force_exit) {
        execute_command();
        //TODO: need to add more things?
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
    //TODO: add here things to do on deletion
    exit(error_code);
}

error_code_t read_pipe(){
    int8_t error = read(rcv_requests_fd, buffer_read, MAX_REQUEST_SIZE);
    if(error < 0){
        return UNABLE_TO_READ_PIPE;
    }
    else{
        error_code_t parse_error = parse_request(&request, buffer_read, MAX_REQUEST_SIZE);
        if(parse_error != OK){
            return parse_error;
        }
    }
    return OK;
}

void write_pipe(){
    error_code_t error = format_response(&response, buffer_write, MAX_RESPONSE_SIZE);

    if(error != OK){
        print_error(STDERR_FILENO, error, id, "while formatting response");
    }
    else if(write(snd_responses_fd, buffer_write, MAX_RESPONSE_SIZE)<0){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

void execute_command(){
    command_code_t code = request.command_code;

    response.command_code = NULL_COMMAND; //! response command code

    error_code_t error = read_pipe();
    if(error != OK){
        response.response_code = error;
    }
    else if (request.destination != id){
        response.response_code = DESTINATION_ID_MISMATCH;
    }
    else{
        //no errors occurred while parsing the request and the destination is correct
        code = request.command_code;
        response.command_code = code; //! response command code
        response.response_code = OK; //! response error code
        response.arguments_size = 0;

        if(IS_INFO(code)){info_response();}
        else if(IS_LINK(code)){link_response(code);}
        else if((IS_SWITCH(code))){switch_response(code);}
        else if((IS_DELETE(code))){force_exit=true;} //then the response is sent, the while loop finishes and it shutdowns
        else{
            response.response_code = UNEXPECTED_COMMAND;
        }

        int random_processing_time = rand() % (MAX_WAITING - MIN_WAITING +1) + MIN_WAITING;
        sleep(random_processing_time);
        write_pipe();
    }
}

void info_response(){
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[OPEN_SECONDS_ARGUMENT] = (state==STATE_CLOSED ? seconds_open : SECONDS_OPEN)/60; //TODO change with OPEN_MINUTES_ARGUMENT
    response.arguments_size = MAX_WINDOW_ARGUMENTS;
}

void link_response(command_code_t code){
    if(LINK_SUBCOMMAND(code)==LINK_CHANGE_PARENT){
        //the request argument is the parent id
        error_code_t error = change_snd_responses_pipe(request.argument,&snd_responses_fd);
        response.arguments[REQUEST_ARGUMENT] = request.argument; //to give always a feedback
        response.arguments_size = 1;

        if(error==OK){
            if(HAS_PARENT_CHANGED(request.argument)){
                parent_id = request.argument;
            }
        }
        else{response.response_code = error;}
    }
    else{response.response_code = UNEXPECTED_COMMAND;}
}

//i have 2 switch (open,close -> on/off)
//there are 2 combinations that are useless 
void switch_response(command_code_t code){
    if(SWITCH_LABEL(code)==SWITCH_OPEN && SWITCH_POSITION(code)==POSITION_ON){
        if(HAS_STATE_CHANGED(SWITCH_OPEN)){
            state = STATE_OPEN;
            time(&last_opened);
        }
    }
    else if(SWITCH_LABEL(code)==SWITCH_CLOSE && SWITCH_POSITION(code)==POSITION_ON){
        if(HAS_STATE_CHANGED(SWITCH_CLOSE)){
            state = STATE_CLOSED;
            time(&last_closed);
            seconds_open = last_closed - last_opened; //! if it's closed then this is the time that it has remained open
        }
    }
    else if((!(NO_ACTION_SWITCH_CLOSE)) && (!(NO_ACTION_SWITCH_OPEN))){
        response.response_code = UNEXPECTED_COMMAND;
    }
}

//response: id command_code response_code arguments

//! switch iff
//make: *** [Makefile:25: default] Segmentation fault (core dumped)

//! info on
//Request: 1 72
//Response: 1 72 0 0 0