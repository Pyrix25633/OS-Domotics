#define _XOPEN_SOURCE 700

#include "timer.h"

//start - make FILE="timer" (ARGS="id" only to test, then it's given by the controller)
//print_error only if i can't send an error response

//response: id command_code response_code arguments

//! the timer is a control device, so it also has the pipe where the child writes its responses

// - Explicit device data -

device_id_t id;
device_type_t device_type = TIMER_DEVICE; //declared in the change-parent response, may become TIMER|child_type (ask Mattia)
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

// the controller starts a timer process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    //the last argument is not NULL because the timer is a control device, so the pipe for the child is created too
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, &rcv_responses_child_fd);

    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler);

    srand(time(NULL)); //set random seed with the current time so it's always different

    error_code_t error_code;
    //TODO start the thread that reads the responses of the child and the one that fires the schedule
    //for now only the requests for the timer itself are handled, one by one in order of arrival
    while(!force_exit) {
        error_code = execute_command();
    }
    handle_shutdown(error_code);
}

void handle_shutdown(error_code_t error) {
    //the child down pipe is only opened for writing, the child owns it, so it is just closed and not deleted
    if(has_child && close(snd_requests_child_fd) < 0){
        print_error(STDERR_FILENO, UNABLE_TO_CLOSE_PIPE, id, "while closing the child requests pipe");
    }
    //control device: the last argument is the child pipe and not NO_FILE_DESCRIPTOR, so it is closed and deleted too
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, rcv_responses_child_fd);
    if(IS_ERROR(error_code)){
        //prints the error on standard error, best practice to do
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    else{ error_code = error; } //no closing error: return the code that caused the shutdown
    //TODO cancel the child and the schedule threads
    exit(error_code);
}

//SIGTERM is used by the controller for a clean deletion, the device shuts down returning the code
void sigterm_handler() {
    handle_shutdown(UNEXPECTED_SHUTDOWN);
}

//SIGPIPE means a write to a pipe with no reader, it is a critical error, the device shuts down
void sigpipe_handler() {
    handle_shutdown(BROKEN_PIPE);
}

error_code_t read_pipe(){
    ssize_t size = read(rcv_requests_fd, buffer_read, MAX_REQUEST_SIZE);

    if(size == 0){ //the write end was closed, no more requests will arrive
        force_exit = true;
        return UNEXPECTED_END_OF_FILE;
    }
    if(size != MAX_REQUEST_SIZE){ //a wrong number of bytes was read, the message is not a valid fixed-size one
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
    response.command_code = NULL_COMMAND; //default, overwritten below when a valid request is parsed
    response.arguments_size = 0;

    error_code_t error_code = read_pipe();
    if(IS_ERROR(error_code)){
        response.response_code = error_code;
    }
    else if(request.destination != id) {
        //the request is not for the timer, so it is for the child or for something under it
        if(has_child){
            //buffer_read has been modified by parse_request, so the request is rebuilt from the struct
            error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
            if(!IS_ERROR(forward_code) && write(snd_requests_child_fd, buffer_read, MAX_REQUEST_SIZE) == MAX_REQUEST_SIZE){
                return error_code; //forwarded: the response is sent by the destination and not by the timer
            }
            //the forwarding failed, so the destination will not answer: the timer answers with an error itself
            response.response_code = UNABLE_TO_WRITE_PIPE;
        }
        else{
            //no child: nobody below can handle it, only the timer can answer
            response.response_code = DEVICE_NOT_FOUND;
        }
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

    return error_code;
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
        response.arguments[PARENT_ID_ARGUMENT] = new_parent_id; //to give always a feedback
        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type; //the parent learns the child type from this response
        response.arguments_size = 2;

        if(parent_id!=new_parent_id){
            response.response_code = change_snd_responses_pipe(new_parent_id,&snd_responses_fd);
            if(response.response_code == OK){
                parent_id = new_parent_id;
            }
        }
        //TODO if the timer already has a child, replay its "add" to the new parent (step 3)
    }
    else if(LINK_SUBCOMMAND(request.command_code)==LINK_REMOVE_CHILD){
        //the request argument is the child id to remove
        response.arguments[CHILD_ID_ARGUMENT] = request.argument; //to give always a feedback
        response.arguments_size = 1;
        if(has_child && request.argument == child_id){
            if(close(snd_requests_child_fd) < 0){
                response.response_code = UNABLE_TO_CLOSE_PIPE;
            }
            has_child = false;
        }
        else{
            response.response_code = DEVICE_NOT_FOUND;
        }
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
    //the timer now gains a child from the child's change-parent response, not from an add-child request (step 3)
}

//begin and end are minutes from midnight, the professor said that there are no timers across midnight
//so the only invalid case is begin > end (as stated in the spec, section 2.2.8), so begin == end is allowed
void create_registry_response(){
    response.arguments[REGISTRY_ARGUMENT] = request.argument; //to give always a feedback
    response.arguments_size = 1;

    if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_BEGIN){
        if(request.argument <= end){ //end is always smaller than MINUTES_IN_A_DAY, so begin is too
            begin = request.argument;
        }
        else{
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_END){
        if(request.argument >= begin && request.argument < MINUTES_IN_A_DAY){
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

//opening in writing does not block because the child already opened its down pipe in reading at startup
error_code_t open_child_requests_pipe(device_id_t child_id, int *snd_requests_child_fd){
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(child_id, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (*snd_requests_child_fd = open(name, O_WRONLY)) < 0){
        return UNABLE_TO_OPEN_PIPE;
    }
    return OK;
}
