#define _XOPEN_SOURCE 700

#include "bulb.h"

//start - make FILE="bulb" (ARGS="id" only to test, then it's given by the controller)
//print_error only if i can't send an error response

//response: id command_code response_code arguments

// - Explicit device data -

device_id_t id;
leaf_device_state_t state = STATE_OFF;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
time_t last_turned_on;  //returns the time in seconds
time_t last_turned_off;

// - IPC data -

int rcv_requests_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd; //write is non blocking  - pipe to send responses to parent or manual commands
bool force_exit = false;
request_t request; //destination, command_code, argument
response_t response;
char buffer_read[MAX_REQUEST_SIZE];   //buffer to read the request from the pipe
char buffer_write[MAX_RESPONSE_SIZE]; //buffer to write the response before send it to the pipe

// - Signal handler -

struct sigaction action_handler; //to set what to do when a signal occurs

// the controller starts a bulb process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    action_handler.sa_handler = handle_shutdown; //set the function to be called when the signal occurs
    sigaction(SIGTERM, &action_handler, NULL);

    srand(time(NULL)); //set random seed with the current time so it's always different

    //the main thread is blocked waiting for requests in a loop (while the exit is not forced by the delete command)
    //when a request is received it is executed, one by one in order of arrival
    while(!force_exit) {
        execute_command();
    }
    handle_shutdown();
}

void handle_shutdown() {
    //leaf device: it has no children pipe, so the last argument is NO_FILE_DESCRIPTOR
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
    else if(size == 0){ //the write end was closed, no more requests will arrive
        force_exit = true;
        return UNEXPECTED_END_OF_FILE;
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
    response.command_code = NULL_COMMAND; //default, overwritten below when a valid request is parsed
    response.arguments_size = 0;

    error_code_t error_code = read_pipe();
    if(error_code != OK){
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
        else if((IS_DELETE(code))) { force_exit=true; } //the response is still sent, then the while loop finishes and it shutdowns
        else{
            response.response_code = UNEXPECTED_COMMAND;
        }
    }

    //the delay is applied before responding, for any command, error responses included
    simulate_processing_time();
    write_pipe();
}

void create_info_response(){
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[ON_SECONDS_ARGUMENT] = (state==STATE_OFF ? LAST_SECONDS_ON : CURRENT_SECONDS_ON);
    response.arguments_size = MAX_BULB_ARGUMENTS;
}

void create_link_response(){
    if(LINK_SUBCOMMAND(request.command_code)==LINK_CHANGE_PARENT){
        //the request argument is the new parent id
        device_id_t new_parent_id = request.argument;
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

//the bulb has a single switch (power -> on/off), both positions cause an action
void create_switch_response(){
    if(SWITCH_LABEL(request.command_code)==SWITCH_POWER && SWITCH_POSITION(request.command_code)==POSITION_ON){
        if(state != STATE_ON){
            state = STATE_ON;
            time(&last_turned_on);
        }
    }
    else if(SWITCH_LABEL(request.command_code)==SWITCH_POWER && SWITCH_POSITION(request.command_code)==POSITION_OFF){
        if(state != STATE_OFF){
            state = STATE_OFF;
            time(&last_turned_off);
        }
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}
