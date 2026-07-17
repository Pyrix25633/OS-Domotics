#define _XOPEN_SOURCE 700

#include "timer.h"

//start - make FILE="timer" (ARGS="id" only to test, then it's given by the controller)
//print_error only if i can't send an error response

//response: id command_code response_code arguments

//! the timer is a control device, so it also has the pipe where the child writes its responses

// - Explicit device data -

device_id_t id;
control_device_state_t state = STATE_OFF; //it mirrors the state of the child
u_int16_t begin = DEFAULT_BEGIN; //activation time, minutes from midnight
u_int16_t end = DEFAULT_END;     //deactivation time, minutes from midnight

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
bool has_child = false;   //the timer controls a single device, but it can also have none
device_id_t child_id;     //valid only if has_child is true
device_type_t child_type; //needed to know which switch label to send to the child

// - IPC data -

int rcv_requests_fd;        //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd;       //write is non blocking  - pipe to send responses to parent or manual commands
int rcv_responses_child_fd; //read is blocking       - pipe where the child writes its responses
//this one is not opened by start_device_fifos: every device creates its own down pipe, so the timer can
//open it only when it gets a child, in the same way change_snd_responses_pipe does for the parent
int snd_requests_child_fd;  //write is non blocking  - pipe to send requests down to the child, valid only if has_child
bool force_exit = false;
request_t request; //destination, command_code, argument
response_t response;
char buffer_read[MAX_REQUEST_SIZE];   //buffer to read the request from the pipe
char buffer_write[MAX_RESPONSE_SIZE]; //buffer to write the response before send it to the pipe

// - Signal handler -

struct sigaction action_handler; //to set what to do when a signal occurs

// the controller starts a timer process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    //the last argument is not NULL because the timer is a control device, so the pipe for the child is created too
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, &rcv_responses_child_fd);

    action_handler.sa_handler = handle_shutdown; //set the function to be called when the signal occurs
    sigaction(SIGTERM, &action_handler, NULL);

    srand(time(NULL)); //set random seed with the current time so it's always different

    //TODO start the thread that reads the responses of the child and the one that fires the schedule
    //for now only the requests for the timer itself are handled, one by one in order of arrival
    while(!force_exit) {
        execute_command();
    }
    handle_shutdown();
}

void handle_shutdown() {
    //control device: the last argument is the child pipe and not NO_FILE_DESCRIPTOR, so it is closed and deleted too
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, rcv_responses_child_fd);
    if(error_code != OK){
        //prints the error on standard error, best practice to do
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    //TODO cancel the child and the schedule threads
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
        //the request is not for the timer, so it is for the child or for something under it
        //TODO forward the request down to the child, the response is sent by the destination and not by the timer
        //TODO if there is no child the timer has to answer with an error, nobody else can
        return;
    }
    else{
        //no errors occurred while parsing the request and the destination is correct
        code = request.command_code;
        response.command_code = code;
        response.response_code = OK;

        if(IS_INFO(code)) { create_info_response(); }
        else if(IS_LINK(code)) { create_link_response(); }
        else if(IS_REGISTRY(code)) { create_registry_response(); }
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
    response.arguments[BEGIN_ARGUMENT] = begin;
    response.arguments[END_ARGUMENT] = end;
    response.arguments_size = MAX_TIMER_ARGUMENTS;
    //? the position 1 is not used by the timer, the positions of begin and end are shared with the fridge registry
    response.arguments[1] = 0;
    //TODO the state can not be cached, it has to be asked to the child to detect a manual override
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
    //TODO LINK_NEW_CHILD and LINK_DELETE_CHILD, the timer accepts a single child and needs to know its type
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}

//begin and end are minutes from midnight, the professor said that there are no timers across midnight
//so it is enough to check that begin comes before end
void create_registry_response(){
    response.arguments[REQUEST_ARGUMENT] = request.argument; //to give always a feedback
    response.arguments_size = 1;

    if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_BEGIN){
        if(request.argument < end){ //end is always smaller than MINUTES_IN_A_DAY, so begin is too
            begin = request.argument;
        }
        else{
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_END){
        if(request.argument > begin && request.argument < MINUTES_IN_A_DAY){
            end = request.argument;
        }
        else{
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else{
        response.arguments_size = 0;
        response.response_code = UNEXPECTED_COMMAND;
    }
    //TODO the schedule thread is waiting on the old times, it has to be woken up to use the new ones
}

//the timer mirrors the state of its child, so a switch is propagated down and it also clears a manual override
void create_switch_response(){
    //TODO propagate the switch to the child, the label depends on its type
    //(power for a bulb, open/close for a window or a fridge), so has_child and child_type are needed
    response.response_code = UNEXPECTED_COMMAND; //temporary, until the child is handled
}
