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
routing_table_t routing_table; //tracks the subtree below a control-device child, to replay it on re-parent
bool parent_changed = false; //set when a link changes the parent, so the child is replayed after the own response

// - Mirroring data -

bool awaiting_child_response = false; //shared: true while the timer waits for the child reply to its own request
command_code_t awaited_command;       //shared: the command sent to the child, to match its reply in the bottom-up thread
bool defer_response = false;          //main thread only: the response will come from the child reply, not from execute_command

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
    init_routing_table(routing_table); //empty until a control-device child is acquired, a leaf child never uses it

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
    if(has_child){
        if(close(snd_requests_child_fd) < 0){
            print_error(STDERR_FILENO, UNABLE_TO_CLOSE_PIPE, id, "while closing the child requests pipe");
        }
        //free the routing table nodes of a control-device child subtree, the threads are already joined so no lock
        remove_routing_data(routing_table, child_id, id);
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
    
    //a control-device child brings its own subtree: it is tracked in the routing table so the whole branch can be
    //replayed to a new parent, a leaf child has no descendants and needs only the scalars above (hybrid approach)
    
    //the && short-circuits: for a leaf child, IS_CONTROL is false and the insert is never evaluated
    if((IS_CONTROL(new_child_type))
        && IS_ERROR(insert_direct_routing_data(routing_table, new_child_id, new_child_type, id, new_child_fd))){
        print_error(STDERR_FILENO, UNABLE_TO_ALLOCATE_HEAP, id, "while adding the child to the routing table");
    }
    has_child = true; //set last, the main thread checks it before using the other child scalars
    pthread_mutex_unlock(&data_mutex);
}

//a replayed change-parent response of a deeper descendant (its parent is not the timer) is recorded in the
//routing table under its own parent, so later the whole subtree can be replayed to a new parent
void acquire_descendant(response_t *child_response){
    command_code_t code = child_response->command_code;
    if(!((IS_LINK(code)) && LINK_SUBCOMMAND(code) == LINK_CHANGE_PARENT)){
        return;
    }
    //a failed response is ignored, the direct child is not a descendant and is handled by acquire_child
    if(child_response->response_code != OK || child_response->arguments[PARENT_ID_ARGUMENT] == id){
        return;
    }
    device_id_t descendant_id = child_response->source;
    device_type_t descendant_type = child_response->arguments[DEVICE_TYPE_ARGUMENT];
    device_id_t descendant_parent = child_response->arguments[PARENT_ID_ARGUMENT];

    //insert_indirect_routing_data looks up the parent in the table first: if it is missing it returns
    //ROUTE_NOT_FOUND and inserts nothing, meaning the device is not part of the timer subtree, so it is skipped.
    //In the correct top-down replay the parent is always inserted before its children, so this is defensive
    pthread_mutex_lock(&data_mutex);
    error_code_t error_code = insert_indirect_routing_data(routing_table, descendant_id, descendant_type, descendant_parent);
    pthread_mutex_unlock(&data_mutex);
    //ROUTE_NOT_FOUND is the expected "not in the subtree" outcome and is ignored, only real errors are printed
    if(IS_ERROR(error_code) && error_code != ROUTE_NOT_FOUND){
        print_error(STDERR_FILENO, error_code, id, "while adding a descendant to the routing table");
    }
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
            //a change-parent response of a deeper descendant is recorded in the routing table for the subtree replay
            acquire_descendant(&child_response);
            //a reply to a request the timer sent for mirroring is turned into the timer own response, not forwarded
            if(handle_own_reply(&child_response)){
                continue;
            }
        }
        //forward the child response to the parent; guarded because the main thread shares the parent pipe
        pthread_mutex_lock(&data_mutex);
        if(write(snd_responses_fd, child_buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
            print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while forwarding a child response");
        }
        pthread_mutex_unlock(&data_mutex);
    }
    return NULL;
}

//if the child response is the reply to a request the timer made for mirroring, it is turned into the timer own
//response (source set to the timer) and sent up, and true is returned so the caller does not forward it too
bool handle_own_reply(response_t *child_response){
    pthread_mutex_lock(&data_mutex);
    bool ours = awaiting_child_response
        && (child_response->command_code & COMMAND_MASK) == (awaited_command & COMMAND_MASK);
    if(ours){
        awaiting_child_response = false;
    }
    pthread_mutex_unlock(&data_mutex);
    if(!ours){
        return false;
    }

    child_response->source = id; //the parent gets the response from the timer, not from the child
    if(IS_SWITCH(child_response->command_code)){
        child_response->arguments_size = 0; //a switch response carries no arguments
    }
    else if(IS_INFO(child_response->command_code)){
        //the child info reply already carries its live state in the first argument, the following positions
        //(num, begin, end) are the timer own and replace whatever the child put after the state
        pthread_mutex_lock(&data_mutex);
        //a live state different from the last one the timer commanded means the child was switched manually,
        //so a manual override is reported instead of a definite state (the next command clears it on its own)
        if(child_response->arguments[STATE_ARGUMENT] != state){
            child_response->arguments[STATE_ARGUMENT] = STATE_MANUAL_OVERRIDE;
        }
        child_response->arguments[NUM_ARGUMENT] = 1; //the info is deferred only when the timer has a child
        child_response->arguments[BEGIN_ARGUMENT] = begin;
        child_response->arguments[END_ARGUMENT] = end;
        pthread_mutex_unlock(&data_mutex);
        child_response->arguments_size = MAX_TIMER_ARGUMENTS;
    }

    char buffer[MAX_RESPONSE_SIZE];
    error_code_t error_code = format_response(child_response, buffer, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting the mirrored response");
        return true;
    }
    pthread_mutex_lock(&data_mutex);
    if(write(snd_responses_fd, buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending the mirrored response");
    }
    pthread_mutex_unlock(&data_mutex);
    return true;
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
    defer_response = false; //reset each command, set only when the response will come from the child reply

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

    //when deferred the response is sent by the child-responses thread on the child reply (delays overlap)
    if(!defer_response){
        //the delay is applied before responding, for any command, error responses included
        simulate_processing_time();
        write_pipe();
    }

    //after the own change-parent response is sent, the branch is replayed so the new parent rebuilds it: a
    //control-device child has its whole subtree in the table, a leaf child (no descendants) is replayed alone
    if(parent_changed){
        pthread_mutex_lock(&data_mutex);
        bool control_child = has_child && (IS_CONTROL(child_type));
        pthread_mutex_unlock(&data_mutex);
        if(control_child){ replay_subtree(); }
        else{ replay_child_add(); }
        parent_changed = false;
    }

    return error_code;
}

//the child state can not be cached, so the info request is propagated to the child and its live state is
//mirrored when it replies (in the bottom-up thread), which is what makes a manual override on the child visible
void create_info_response(){
    //the child scalars are set by the child-responses thread, so they are snapshotted under the lock
    pthread_mutex_lock(&data_mutex);
    bool present = has_child;
    int child_fd = snd_requests_child_fd;
    device_id_t target = child_id;
    pthread_mutex_unlock(&data_mutex);

    if(!present){
        //no child to query, the timer answers on its own with an undefined mirrored state, like the hub does
        response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
        response.arguments[NUM_ARGUMENT] = 0;
        response.arguments[BEGIN_ARGUMENT] = begin;
        response.arguments[END_ARGUMENT] = end;
        response.arguments_size = MAX_TIMER_ARGUMENTS;
        return;
    }
    //propagate the info down, rebuilt from the struct with the child as destination
    request.destination = target;
    error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
    if(IS_ERROR(forward_code) || write(child_fd, buffer_read, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
        response.response_code = UNABLE_TO_WRITE_PIPE;
        return;
    }
    //the child reply is awaited: the info response is built and sent by the bottom-up thread, not here
    pthread_mutex_lock(&data_mutex);
    awaiting_child_response = true;
    awaited_command = request.command_code;
    pthread_mutex_unlock(&data_mutex);
    defer_response = true;
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
            //the whole subtree leaves with the child, so it is removed from the table (cascades to all
            //descendants, a leaf child was never tracked and this is a no-op)
            remove_routing_data(routing_table, child_id, id);
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

//re-announces the whole tracked subtree to the new parent, one faked change-parent response per node, each with
//the node own parent and type, so the new parent and the chain above rebuild every branch; used when the child is
//a control device, find_all_routing_data returns a node always after its parent so the branch is rebuilt top-down
void replay_subtree(){
    response_t node_add;
    node_add.command_code = LINK | LINK_CHANGE_PARENT; //looks like the node own change-parent response
    node_add.response_code = OK;
    node_add.arguments_size = 2;
    char buffer[MAX_RESPONSE_SIZE];

    //the table and snd_responses_fd are shared with the child-responses thread, so the whole traversal is guarded:
    //the node pointer is the cursor for the next find_all_routing_data call and must stay valid until the end
    pthread_mutex_lock(&data_mutex);
    routing_data_t *node = find_all_routing_data(routing_table, id, NULL);
    while(node != NULL){
        node_add.source = node->id;
        node_add.arguments[PARENT_ID_ARGUMENT] = node->parent_id;
        node_add.arguments[DEVICE_TYPE_ARGUMENT] = node->type;
        if(IS_ERROR(format_response(&node_add, buffer, MAX_RESPONSE_SIZE))
            || write(snd_responses_fd, buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE){
            print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while replaying the subtree");
        }
        node = find_all_routing_data(routing_table, id, node);
    }
    pthread_mutex_unlock(&data_mutex);
}

//cancels the schedule thread waiting on the old times and starts a new one that uses the current begin and end
void reschedule(){
    if(schedule_thread_running){
        pthread_cancel(schedule_thread);
        pthread_join(schedule_thread, NULL);
        schedule_thread_running = false;
    }
    if(pthread_create(&schedule_thread, NULL, schedule_handler, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while restarting the schedule thread");
        return;
    }
    schedule_thread_running = true;
}

//begin and end are minutes from midnight, the professor said that there are no timers across midnight
//so the only invalid case is begin > end (as stated in the spec, section 2.2.8), so begin == end is allowed
void create_registry_response(){
    response.arguments[REGISTRY_ARGUMENT] = request.argument; //to give always a feedback
    response.arguments_size = 1;
    bool changed = false;

    //begin and end are read by the schedule thread, so they are validated and updated under the lock
    pthread_mutex_lock(&data_mutex);
    if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_BEGIN){
        if(request.argument <= end){ //end is always smaller than MINUTES_IN_A_DAY, so begin is too
            begin = request.argument;
            changed = true;
        }
        else{
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else if(REGISTRY_SUBCOMMAND(request.command_code)==REGISTRY_END){
        if(request.argument >= begin && request.argument < MINUTES_IN_A_DAY){
            end = request.argument;
            changed = true;
        }
        else{
            response.response_code = INVALID_REQUEST_ARGUMENT;
        }
    }
    else{
        response.arguments_size = 0;
        response.response_code = UNEXPECTED_COMMAND;
    }
    pthread_mutex_unlock(&data_mutex);

    //the schedule thread was sleeping on the old times, it is restarted so it uses the new begin or end
    if(changed){
        reschedule();
    }
}

//the timer mirrors the state of its child, so a switch is propagated down; the state becomes consistent after
//the propagation, and the manual override is cleared because the mirrored state is set to the commanded one
void create_switch_response(){
    command_code_t code = request.command_code;
    pthread_mutex_lock(&data_mutex);
    bool present = has_child;
    device_type_t type = child_type;
    int child_fd = snd_requests_child_fd;
    device_id_t target = child_id;
    pthread_mutex_unlock(&data_mutex);
    if(!present){
        response.response_code = DEVICE_NOT_FOUND; //no child to mirror
        return;
    }
    //the switch label must match the child type (power for a bulb, open or close for a window or a fridge)
    bool valid = (IS_BULB_LIKE(type) && SWITCH_LABEL(code)==SWITCH_POWER)
        || ((IS_WINDOW_LIKE(type) || IS_FRIDGE_LIKE(type))
            && (SWITCH_LABEL(code)==SWITCH_OPEN || SWITCH_LABEL(code)==SWITCH_CLOSE));
    if(!valid){
        response.response_code = UNEXPECTED_COMMAND;
        return;
    }
    //propagate the switch down, rebuilt from the struct with the child as destination
    request.destination = target;
    error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
    if(IS_ERROR(forward_code) || write(child_fd, buffer_read, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
        response.response_code = UNABLE_TO_WRITE_PIPE;
        return;
    }
    //mirror the state the switch produces on the child; open/close in the off position is a no-op
    pthread_mutex_lock(&data_mutex);
    if(SWITCH_POSITION(code)==POSITION_ON){
        state = (SWITCH_LABEL(code)==SWITCH_CLOSE) ? STATE_OFF : STATE_ON;
    }
    else if(SWITCH_LABEL(code)==SWITCH_POWER){
        state = STATE_OFF;
    }
    //the child reply is awaited: the response is sent by the bottom-up thread, not here
    awaiting_child_response = true;
    awaited_command = code;
    pthread_mutex_unlock(&data_mutex);
    defer_response = true;
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
