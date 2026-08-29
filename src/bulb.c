#define _XOPEN_SOURCE 700

#include "bulb.h"

// - Explicit device data -

device_id_t id;
leaf_device_state_t state = STATE_OFF;
device_type_t device_type = BULB_DEVICE;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
time_t last_turned_on;  //returns the time in seconds
time_t last_turned_off;

// - IPC data -

int rcv_requests_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd; //write is non blocking  - pipe to send responses to parent or manual commands
bool force_exit = false;
request_t request;
response_t response;
char buffer_read[MAX_REQUEST_SIZE];   //buffer to read the request from the pipe
char buffer_write[MAX_RESPONSE_SIZE]; //buffer to write the response before send it to the pipe

// the controller starts a bulb process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    //the handlers are set before opening the pipes: the open of the down fifo blocks until the controller
    //opens it for writing, and a signal arriving in the meantime would kill the process with no clean shutdown
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGINT, sigterm_handler); //same handler as SIGTERM: a Ctrl+C reaches the whole process group
    set_signal_handler(SIGPIPE, sigpipe_handler);

    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, NULL);

    //the pid is mixed in because time(NULL) has a one second granularity and the devices are created in a
    //row, so seeding with the time alone gives the same processing delays to every device born in the same second
    srand(time(NULL) ^ getpid());

    error_code_t error_code = OK;
    //the main thread is blocked waiting for requests in a loop (while the exit is not forced by the delete command)
    //when a request is received it is executed, one by one in order of arrival
    while(!force_exit) {
        error_code = execute_command();
    }
    handle_shutdown(error_code);
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, NO_FILE_DESCRIPTOR);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    else{ error_code = error; }
    exit(error_code);
}

void sigterm_handler(int sig_num) {
    (void)sig_num;
    handle_shutdown(UNEXPECTED_SHUTDOWN);
}

void sigpipe_handler(int sig_num) {
    (void)sig_num;
    handle_shutdown(BROKEN_PIPE);
}

error_code_t read_pipe(){
    ssize_t size = read(rcv_requests_fd, buffer_read, MAX_REQUEST_SIZE);

    if(size == 0){ //the write end was closed, no more requests will arrive
        force_exit = true;
        return UNEXPECTED_END_OF_FILE;
    }
    if(size != MAX_REQUEST_SIZE){
        return UNABLE_TO_READ_PIPE;
    }
    return parse_request(&request, buffer_read, MAX_REQUEST_SIZE);
}

void write_pipe(){
    error_code_t error_code = format_response(&response, buffer_write, MAX_RESPONSE_SIZE);

    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting response");
    }
    else if(write(snd_responses_fd, buffer_write, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

error_code_t execute_command(){
    command_code_t code;
    response.command_code = NULL_COMMAND;
    response.arguments_size = 0;

    error_code_t error_code = read_pipe();
    if(IS_ERROR(error_code)){
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

    simulate_processing_time();
    write_pipe();

    return error_code;
}

void create_info_response(){
    response.arguments[STATE_ARGUMENT] = state;
    response.arguments[ON_SECONDS_ARGUMENT] = (state==STATE_OFF ? LAST_SECONDS_ON : CURRENT_SECONDS_ON)/60;
    response.arguments_size = MAX_BULB_ARGUMENTS;
}

void create_link_response(){
    if(LINK_SUBCOMMAND(request.command_code)==LINK_CHANGE_PARENT){
        //the request argument is the new parent id
        device_id_t new_parent_id = request.argument;
        response.arguments[PARENT_ID_ARGUMENT] = new_parent_id; //to give always a feedback
        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type;
        response.arguments_size = 2;

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
    if(SWITCH_LABEL(request.command_code)==SWITCH_POWER){
        if(SWITCH_POSITION(request.command_code)==POSITION_ON){
            if(state != STATE_ON){
                state = STATE_ON;
                time(&last_turned_on);
            }
        }
        else if(SWITCH_POSITION(request.command_code)==POSITION_OFF){
            if(state != STATE_OFF){
                state = STATE_OFF;
                time(&last_turned_off);
            }
        }
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}
