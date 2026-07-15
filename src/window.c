#define _XOPEN_SOURCE 700

#include "window.h"

//start - make FILE="window"
// print_error only if i can't send an error response

// - Explicit device data -

device_id_t id;
leaf_device_state_t state = STATE_CLOSED;
u_int32_t seconds_open = INITIAL_SECONDS_OPEN;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;

// Timestamps needed to calculate the open time

time_t last_opened; //returns the time in seconds
time_t last_closed;


//TODO comment

request_t request; //destination, command_code, argument
response_t response;
char buffer_read[MAX_REQUEST_SIZE]; //buffer to read the request from the pipe

// - IPC data -

int rcv_requests_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd; //write is non blocking  - pipe to send responses to parent or manual commands

// - Signal handler -

struct sigaction action_handler; //to set what to do when a signal occurs

// the controller starts a window process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id; //! response source
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown; //set the function to be called when the signal occurs
    sigaction(SIGTERM, &action_handler, NULL);

    //the main thread is blocked waiting for requests in a loop
    //when a request is received it is executed, one by one in order of arrival
    while(true) {

    }

    return OK;
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
        return UNABLE_TO_OPEN_PIPE; //TODO to substitute with UNABLE_TO_READ_PIPE
    }
    else{
        error_code_t parse_error = parse_request(&request, buffer_read, MAX_REQUEST_SIZE);
        if(parse_error != OK){
            return parse_error;
        }
    }
    return OK;
}

void execute_command(){
    error_code_t error_code;

    //default values for the response, to be changed if needed

    command_code_t code = NULL_COMMAND;

    //TODO for every case I put the correct value and only at the end i create the response with the right parameters

    error_code_t error = read_pipe();
    if(error != OK){
        error_code = error;
    }
    else if (request.destination != id){
        error_code = DESTINATION_ID_MISMATCH;
    }
    else{
        //no errors occurred while parsing the request and the destination is correct
        code = request.command_code;
        response.command_code = code; //! response command code
        response.response_code = OK; //! response error code

        //TODO
        if(IS_INFO(code)){info_response();}
        else if(IS_LINK(code)){link_response(code);}
        else if((IS_SWITCH(code))){switch_response(code);}
        else if((IS_REGISTRY(code))){}
        else if((IS_DELETE(code))){}
        else{} //TODO unexpected command
    }
}

//TODO wait a random time before responding

//TODO verify if the response error code is always ok or not, if it is then I set it with
//TODO the response command code

//! I need to track what i have written and what not for the response

void info_response(){
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[OPEN_HOURS_ARGUMENT] = seconds_open;
    response.arguments_size = MAX_WINDOW_ARGUMENTS;
}

void link_response(command_code_t code){
    if(LINK_SUBCOMMAND(code)==LINK_CHANGE_PARENT){
        change_snd_responses_pipe(request.argument,&snd_responses_fd); //the argument is the new parent id
        if(IS_PARENT_CHANGED(request.argument)){
            parent_id = request.argument;
            response.arguments[REQUEST_ARGUMENT] = parent_id;
            response.arguments_size = 1;
        }
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}

//i have 2 switch (open,close -> on/off)
void switch_response(command_code_t code){
    if(SWITCH_LABEL(code)==SWITCH_OPEN && SWITCH_POSITION(code)==POSITION_ON){
        if(IS_STATE_CHANGED(SWITCH_OPEN)){
            state = SWITCH_OPEN;
            time(&last_opened); 
        }
    }
    else if(SWITCH_LABEL(code)==SWITCH_CLOSE && SWITCH_POSITION(code)==POSITION_ON){
        if(IS_STATE_CHANGED(SWITCH_CLOSE)){
            state = SWITCH_CLOSE;
            time(&last_closed); //TODO verify is it's from the last or in total
            seconds_open = last_closed - last_opened;
        }
    }
    else if(SWITCH_LABEL(code)==SWITCH_POWER){
        response.response_code = UNEXPECTED_COMMAND;
    }
}






