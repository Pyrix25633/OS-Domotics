#define _XOPEN_SOURCE 700

#include "timer.h"

//start - make FILE="timer" (ARGS="id" only to test, then it's given by the controller)
//print_error only if i can't send an error response

//response: id command_code response_code arguments

//! the timer is a control device, so it also has the pipe where the child writes its responses

// - Explicit device data -

device_id_t id;
device_type_t device_type = TIMER_DEVICE; //declared in the change-parent response so the parent knows the timer type
control_device_state_t state = STATE_OFF; //it mirrors the state of the child
u_int16_t begin = DEFAULT_BEGIN; //activation time, minutes from midnight
u_int16_t end = DEFAULT_END;     //deactivation time, minutes from midnight

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
bool has_child = false;   //the timer controls a single device, but it can also have none
device_id_t child_id;     //valid only if has_child is true
device_type_t child_type; //needed to know which switch label to send to the child
bool parent_changed = false; //set when a link changes the parent, so the child is replayed after the own response

// - IPC data -

int rcv_requests_fd;        //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd;       //write is non blocking  - pipe to send responses to parent or manual commands
int rcv_responses_child_fd; //read is blocking       - pipe where the child writes its responses
//this one is not opened by start_device_fifos: every device creates its own down pipe, so the timer can
//open it only when it gets a child, in the same way change_snd_responses_pipe does for the parent
int snd_requests_child_fd;  //write is non blocking  - pipe to send requests down to the child, valid only if has_child
volatile bool force_exit = false; //set by the main thread, read by the child-responses thread, so volatile
request_t request; //destination, command_code, argument
response_t response;
char buffer_read[MAX_REQUEST_SIZE];   //buffer to read the request from the pipe
char buffer_write[MAX_RESPONSE_SIZE]; //buffer to write the response before send it to the pipe

// - Thread -

pthread_t child_responses_thread;           //bottom-up thread: reads the child responses and forwards them up
volatile bool child_thread_running = false; //true once the thread exists, to cancel it safely at shutdown
pthread_t schedule_thread;                     //schedule thread: switches the child on at begin and off at end
volatile bool schedule_thread_running = false; //true once the thread exists, to cancel it safely at shutdown
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER; //guards the state shared between the threads (pipes, child scalars, begin, end, state)

// the controller starts a timer process with the exec command using the executable file in /bin
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv); //id given by the controller when it does the exec
    response.source = id;
    //the last argument is not NULL because the timer is a control device, so the pipe for the child is created too
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, &rcv_responses_child_fd);

    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler);

    srand(time(NULL)); //set random seed with the current time so it's always different

    //start the bottom-up thread that reads the child responses and forwards them up to the parent
    if(pthread_create(&child_responses_thread, NULL, child_responses_handler, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while creating the child responses thread");
        handle_shutdown(UNABLE_TO_CREATE_THREAD);
    }
    child_thread_running = true;

    //start the schedule thread that switches the child on at begin and off at end
    if(pthread_create(&schedule_thread, NULL, schedule_handler, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while creating the schedule thread");
        handle_shutdown(UNABLE_TO_CREATE_THREAD);
    }
    schedule_thread_running = true;

    error_code_t error_code;
    //the main thread handles the requests coming from the parent, one by one in order of arrival
    while(!force_exit) {
        error_code = execute_command();
    }
    handle_shutdown(error_code);
}

void handle_shutdown(error_code_t error) {
    //the child-responses thread is blocked on a read that never returns EOF (up pipe in O_RDWR),
    //so it is cancelled and joined before closing the pipes it uses
    if(child_thread_running){
        pthread_cancel(child_responses_thread);
        pthread_join(child_responses_thread, NULL);
        child_thread_running = false;
    }
    //the schedule thread may be sleeping or writing to the child, it is cancelled and joined before its pipe is closed
    if(schedule_thread_running){
        pthread_cancel(schedule_thread);
        pthread_join(schedule_thread, NULL);
        schedule_thread_running = false;
    }
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
        return;
    }
    //the child-responses thread writes to the same parent pipe, and a link can re-point it, so the write is guarded
    pthread_mutex_lock(&data_mutex);
    if(write(snd_responses_fd, buffer_write, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
    pthread_mutex_unlock(&data_mutex);
}

//a change-parent response naming the timer as the new parent identifies the single child to acquire,
//replayed responses of deeper descendants carry another parent id and are only forwarded up
void acquire_child(response_t *child_response){
    command_code_t code = child_response->command_code;
    if(!((IS_LINK(code)) && LINK_SUBCOMMAND(code) == LINK_CHANGE_PARENT)){
        return;
    }
    if(child_response->response_code != OK || child_response->arguments[PARENT_ID_ARGUMENT] != id){
        return;
    }
    device_id_t new_child_id = child_response->source;
    device_type_t new_child_type = child_response->arguments[DEVICE_TYPE_ARGUMENT];
    //opening does not block, the child already opened its down pipe in reading, done outside the lock
    int new_child_fd;
    if(IS_ERROR(open_child_requests_pipe(new_child_id, &new_child_fd))){
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, id, "while opening the child requests pipe");
        return;
    }
    //these scalars are read by the main thread, so they are published together under the lock
    pthread_mutex_lock(&data_mutex);
    child_id = new_child_id;
    child_type = new_child_type;
    snd_requests_child_fd = new_child_fd;
    device_type = TIMER_DEVICE | new_child_type; //the declared type keeps the child leaf bits for the switch label
    has_child = true; //set last, the main thread checks it before using the other child scalars
    pthread_mutex_unlock(&data_mutex);
}

//bottom-up thread: reads the responses coming from the child and forwards them up to the parent
void *child_responses_handler(void *arg){
    (void)arg; //unused
    char child_buffer[MAX_RESPONSE_SIZE]; //raw bytes from the child, forwarded up unchanged
    char parse_buffer[MAX_RESPONSE_SIZE]; //copy to parse, parse_response inserts terminators in the buffer
    response_t child_response;
    while(!force_exit){
        //with the up pipe opened in O_RDWR the read never returns EOF: it blocks until the child writes,
        //or it is interrupted by pthread_cancel at shutdown
        ssize_t size = read(rcv_responses_child_fd, child_buffer, MAX_RESPONSE_SIZE);
        if(size != MAX_RESPONSE_SIZE){
            print_error(STDERR_FILENO, UNABLE_TO_READ_PIPE, id, "while reading a child response");
            continue;
        }
        //the change-parent response of a new child is intercepted to acquire it, parsing a copy of the bytes
        memcpy(parse_buffer, child_buffer, MAX_RESPONSE_SIZE);
        if(!IS_ERROR(parse_response(&child_response, parse_buffer, MAX_RESPONSE_SIZE))){
            acquire_child(&child_response);
        }
        //TODO intercept the responses to the info and switch requests the timer sends to the child for mirroring
        //forward the child response to the parent; guarded because the main thread shares the parent pipe
        pthread_mutex_lock(&data_mutex);
        if(write(snd_responses_fd, child_buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
            print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while forwarding a child response");
        }
        pthread_mutex_unlock(&data_mutex);
    }
    return NULL;
}

//builds the switch command for the child from its type and sends it down, the label depends on the child
//type (power for a bulb, open or close for a window or a fridge), the position turns it on or off
error_code_t send_child_switch(bool activate){
    //the child scalars are shared with the other threads, so they are snapshotted under the lock
    pthread_mutex_lock(&data_mutex);
    bool present = has_child;
    device_id_t target = child_id;
    device_type_t type = child_type;
    int child_fd = snd_requests_child_fd;
    pthread_mutex_unlock(&data_mutex);
    if(!present){
        return CHILD_NOT_FOUND; //nothing to switch
    }

    command_code_t switch_code = SWITCH;
    if(IS_BULB_LIKE(type)){
        //a bulb has a single power switch, the position turns it on or off
        switch_code |= SWITCH_POWER | (activate ? POSITION_ON : POSITION_OFF);
    }
    else{
        //a window or a fridge have separate open and close momentary switches, both triggered on
        switch_code |= (activate ? SWITCH_OPEN : SWITCH_CLOSE) | POSITION_ON;
    }

    request_t child_request;
    child_request.destination = target;
    child_request.command_code = switch_code;
    char buffer[MAX_REQUEST_SIZE];
    error_code_t error_code = format_request(&child_request, buffer, MAX_REQUEST_SIZE);
    if(IS_ERROR(error_code)){
        return error_code;
    }
    if(write(child_fd, buffer, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
        return UNABLE_TO_WRITE_PIPE;
    }
    return OK;
}

//schedule thread: sleeps until the next begin or end and switches the child on or off, repeating each day
void *schedule_handler(void *arg){
    (void)arg; //unused
    while(!force_exit){
        pthread_mutex_lock(&data_mutex);
        u_int16_t local_begin = begin;
        u_int16_t local_end = end;
        pthread_mutex_unlock(&data_mutex);

        //current time of the day in seconds, begin and end are minutes from midnight
        time_t now = time(NULL);
        struct tm *local_time = localtime(&now);
        int now_second = local_time->tm_hour * 3600 + local_time->tm_min * 60 + local_time->tm_sec;
        int begin_second = local_begin * 60;
        int end_second = local_end * 60;

        //before begin wait to switch on, inside the window wait to switch off, after end wait for the next day
        bool activate;
        int target_second;
        if(now_second < begin_second){
            activate = true;  target_second = begin_second;
        }
        else if(now_second < end_second){
            activate = false; target_second = end_second;
        }
        else{
            activate = true;  target_second = begin_second + SECONDS_IN_A_DAY;
        }

        //sleep is a cancellation point, so pthread_cancel can wake the thread at shutdown
        sleep(target_second - now_second);

        //the state is mirrored only if the switch was actually sent to the child
        if(send_child_switch(activate) == OK){
            pthread_mutex_lock(&data_mutex);
            state = activate ? STATE_ON : STATE_OFF;
            pthread_mutex_unlock(&data_mutex);
        }
    }
    return NULL;
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
        //has_child and the child fd are set by the child-responses thread, so they are read under the lock
        pthread_mutex_lock(&data_mutex);
        bool forward = has_child;
        int child_fd = snd_requests_child_fd;
        pthread_mutex_unlock(&data_mutex);
        if(forward){
            //buffer_read has been modified by parse_request, so the request is rebuilt from the struct
            error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
            if(!IS_ERROR(forward_code) && write(child_fd, buffer_read, MAX_REQUEST_SIZE) == MAX_REQUEST_SIZE){
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

    //after the own change-parent response is sent, the child is replayed so the new parent rebuilds the branch
    if(parent_changed){
        replay_child_add();
        parent_changed = false;
    }

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
        //device_type is updated by the child-responses thread when a child is acquired, so it is read under the lock
        pthread_mutex_lock(&data_mutex);
        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type; //the parent learns the timer type from this response
        pthread_mutex_unlock(&data_mutex);
        response.arguments_size = 2;

        if(parent_id!=new_parent_id){
            //the child-responses thread also uses snd_responses_fd, so re-pointing it is guarded
            pthread_mutex_lock(&data_mutex);
            response.response_code = change_snd_responses_pipe(new_parent_id,&snd_responses_fd);
            pthread_mutex_unlock(&data_mutex);
            if(response.response_code == OK){
                parent_id = new_parent_id;
                parent_changed = true; //the child is replayed to the new parent after the timer own response
            }
        }
    }
    else if(LINK_SUBCOMMAND(request.command_code)==LINK_REMOVE_CHILD){
        //the request argument is the child id to remove
        response.arguments[CHILD_ID_ARGUMENT] = request.argument; //to give always a feedback
        response.arguments_size = 1;
        //the child scalars are shared with the child-responses thread, so they are accessed under the lock
        pthread_mutex_lock(&data_mutex);
        if(has_child && request.argument == child_id){
            if(close(snd_requests_child_fd) < 0){
                response.response_code = UNABLE_TO_CLOSE_PIPE;
            }
            has_child = false;
            device_type = TIMER_DEVICE; //without a child the declared type is again the plain timer type
        }
        else{
            response.response_code = DEVICE_NOT_FOUND;
        }
        pthread_mutex_unlock(&data_mutex);
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
    //a child is no longer added here, it is acquired from its change-parent response
}

//re-announces the child to the new parent by faking its change-parent response, so the new parent and the
//chain above rebuild the branch, the source is the child and the declared parent is the timer
void replay_child_add(){
    //the child scalars are shared with the child-responses thread, so they are snapshotted under the lock
    pthread_mutex_lock(&data_mutex);
    bool present = has_child;
    device_id_t replayed_child_id = child_id;
    device_type_t replayed_child_type = child_type;
    pthread_mutex_unlock(&data_mutex);
    if(!present){
        return; //no child, nothing to replay
    }

    response_t child_add;
    child_add.source = replayed_child_id;
    child_add.command_code = LINK | LINK_CHANGE_PARENT; //looks like the child own change-parent response
    child_add.response_code = OK;
    child_add.arguments[PARENT_ID_ARGUMENT] = id;                     //the child parent is the timer
    child_add.arguments[DEVICE_TYPE_ARGUMENT] = replayed_child_type;  //the child declares its own type
    child_add.arguments_size = 2;

    char buffer[MAX_RESPONSE_SIZE];
    error_code_t error_code = format_response(&child_add, buffer, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting the child replay");
        return;
    }
    //snd_responses_fd is shared with the child-responses thread, so the write is guarded
    pthread_mutex_lock(&data_mutex);
    if(write(snd_responses_fd, buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending the child replay");
    }
    pthread_mutex_unlock(&data_mutex);
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
