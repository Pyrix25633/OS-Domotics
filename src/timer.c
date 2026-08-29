#define _XOPEN_SOURCE 700

#include "timer.h"

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

//the parent can send new requests before the child has answered the previous ones, so every command sent
//down is kept until its own reply comes back, not just the last one
pending_node_t *pending_commands = NULL; //shared: linked list of commands sent to the child and not yet answered
bool defer_response = false;          //main thread only: the response will come from the child reply, not from execute_command

// - IPC data -

int rcv_requests_fd;        //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_fd;       //write is non blocking  - pipe to send responses to parent or manual commands
int rcv_responses_child_fd; //read is blocking       - pipe where the child writes its responses
//this one is not opened by start_device_fifos: every device creates its own down pipe, so the timer can
//open it only when it gets a child, in the same way change_snd_responses_pipe does for the parent
int snd_requests_child_fd;  //write is non blocking  - pipe to send requests down to the child, valid only if has_child
volatile bool force_exit = false; //set by the main thread, read by the child-responses thread, so volatile
volatile error_code_t shutdown_code = OK; //set when a failure forces the exit, so main does not report a success
request_t request;
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
    start_device_fifos(id, &rcv_requests_fd, &snd_responses_fd, &rcv_responses_child_fd);
    init_routing_table(routing_table);

    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGINT, sigterm_handler); //same handler as SIGTERM: a Ctrl+C reaches the whole process group
    //a control device writes in two directions, so a broken pipe does not always mean the same thing: the
    //signal is ignored and the failing write reports the error, which is then handled where it happened
    set_signal_handler(SIGPIPE, SIG_IGN);

    //the pid is mixed in because time(NULL) has a one second granularity and the devices are created in a
    //row, so seeding with the time alone gives the same processing delays to every device born in the same second
    srand(time(NULL) ^ getpid());

    if(pthread_create(&child_responses_thread, NULL, child_responses_routine, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while creating the child responses thread");
        handle_shutdown(UNABLE_TO_CREATE_THREAD);
    }
    child_thread_running = true;

    if(pthread_create(&schedule_thread, NULL, schedule_routine, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while creating the schedule thread");
        handle_shutdown(UNABLE_TO_CREATE_THREAD);
    }
    schedule_thread_running = true;

    error_code_t error_code = OK;
    while(!force_exit) {
        error_code = execute_command();
    }
    handle_shutdown(IS_ERROR(shutdown_code) ? shutdown_code : error_code);
}

void handle_shutdown(error_code_t error) {
    //the child-responses thread is blocked on a read that never returns EOF (up pipe in O_RDWR), so it is
    //cancelled and joined before closing the pipes it uses, and it is skipped when the shutdown is requested by
    //that same thread, on the child delete confirmation, because a thread can not cancel and join itself
    if(child_thread_running && !pthread_equal(pthread_self(), child_responses_thread)){
        if(pthread_cancel(child_responses_thread) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_CANCEL_THREAD, id, "while cancelling the child responses thread");
        }
        if(pthread_join(child_responses_thread, NULL) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_JOIN_THREAD, id, "while joining the child responses thread");
        }
        child_thread_running = false;
    }
    if(schedule_thread_running){
        if(pthread_cancel(schedule_thread) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_CANCEL_THREAD, id, "while cancelling the schedule thread");
        }
        if(pthread_join(schedule_thread, NULL) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_JOIN_THREAD, id, "while joining the schedule thread");
        }
        schedule_thread_running = false;
    }
    if(has_child){
        if(close(snd_requests_child_fd) < 0){
            print_error(STDERR_FILENO, UNABLE_TO_CLOSE_PIPE, id, "while closing the child requests pipe");
        }
        remove_routing_data(routing_table, child_id, id);
    }
    error_code_t error_code = end_device_fifos(id, rcv_requests_fd, snd_responses_fd, rcv_responses_child_fd);
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

//the data mutex is statically initialized, so a lock failure should not happen, but if it does the shared state can
//not be accessed safely, so the error is reported and false is returned and the caller must not touch the data,
//and a failed lock is not fatal on its own (the command is answered with an error), while a failed unlock leaves the
//mutex stuck locked and forces the exit
bool lock_data(){
    if(pthread_mutex_lock(&data_mutex) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_LOCK_MUTEX, id, "while locking the shared data");
        return false;
    }
    return true;
}

bool unlock_data(){
    if(pthread_mutex_unlock(&data_mutex) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_UNLOCK_MUTEX, id, "while unlocking the shared data");
        force_exit = true;
        shutdown_code = UNABLE_TO_UNLOCK_MUTEX; //the exit is forced from here, so main has no other way to know why
        return false;
    }
    return true;
}

bool add_pending(command_code_t code){
    pending_node_t *node = malloc(sizeof(pending_node_t));
    if(node == NULL){
        return false;
    }
    node->command_code = code;
    node->next = NULL;
    //appended at the end, so the oldest awaited command of a kind is matched first, keeping the request order
    if(pending_commands == NULL){
        pending_commands = node;
    }
    else{
        pending_node_t *last = pending_commands;
        while(last->next != NULL){
            last = last->next;
        }
        last->next = node;
    }
    return true;
}

//removes the first awaited command matching the given one and tells whether it was found, a child that is a
//control device can complete different commands out of order, so the match is done on the command and not
//simply on the oldest entry, the caller must already hold the lock
bool take_pending(command_code_t code){
    pending_node_t *current = pending_commands;
    pending_node_t *previous = NULL;
    while(current != NULL){
        if((current->command_code & COMMAND_MASK) == (code & COMMAND_MASK)){
            if(previous == NULL){
                pending_commands = current->next;
            }
            else{
                previous->next = current->next;
            }
            free(current);
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
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
        return;
    }
    write_to_parent(buffer_write, "while sending response");
}

//the child-responses thread writes to the same parent pipe, and a link can re-point it, so the write is
//guarded, and with SIGPIPE ignored the write fails instead of terminating the process, but towards the parent
//there is nobody left to report the failure to, so it stays fatal and the timer shuts down as before
void write_to_parent(char *buffer, char *message){
    if(!lock_data()){
        handle_shutdown(UNABLE_TO_LOCK_MUTEX);
        return;
    }
    bool failed = write(snd_responses_fd, buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE;
    unlock_data(); //released before the shutdown, which joins the thread that uses the same mutex
    if(failed){
        print_error(STDERR_FILENO, BROKEN_PIPE, id, message);
        handle_shutdown(BROKEN_PIPE);
    }
}

//a failed write to the child means nobody is reading its pipe anymore, so the child is dropped and the timer
//keeps working reporting the failure, instead of terminating the way a leaf device does on a broken pipe
void release_unreachable_child(){
    if(!lock_data()){
        return;
    }
    if(has_child){
        error_code_t error_code = drop_child();
        if(IS_ERROR(error_code)){
            print_error(STDERR_FILENO, error_code, id, "while releasing the unreachable child");
        }
    }
    unlock_data();
}

//the declared type carries the leaf bits of the child, so the parent knows which switch label the branch takes,
//a leaf child keeps the type it declared, while a control-device child changes its own type as its branch is
//filled or emptied, so its current type is read from the routing table, the caller must already hold the lock
void refresh_child_type(){
    routing_data_t *child = find_routing_data(routing_table, child_id);
    if(child != NULL){
        child_type = child->type;
    }
    device_type = TIMER_DEVICE | CHILD_TYPE(child_type);
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
    int new_child_fd;
    if(IS_ERROR(open_child_requests_pipe(new_child_id, &new_child_fd))){
        print_error(STDERR_FILENO, UNABLE_TO_OPEN_PIPE, id, "while opening the child requests pipe");
        return;
    }
    if(!lock_data()){
        close(new_child_fd); //the fd could not be published, it is closed to avoid a leak
        return;
    }
    child_id = new_child_id;
    child_type = new_child_type;
    snd_requests_child_fd = new_child_fd;

    //a control-device child brings its own subtree: it is tracked in the routing table so the whole branch can be
    //replayed to a new parent, a leaf child has no descendants and needs only the scalars above
    if((IS_CONTROL(new_child_type))
        && IS_ERROR(insert_direct_routing_data(routing_table, new_child_id, new_child_type, id, new_child_fd))){
        print_error(STDERR_FILENO, UNABLE_TO_ALLOCATE_HEAP, id, "while adding the child to the routing table");
    }
    refresh_child_type();
    has_child = true; //set last, the main thread checks it before using the other child scalars
    unlock_data();
}

//a replayed change-parent response of a deeper descendant (its parent is not the timer) is recorded in the
//routing table under its own parent, so later the whole subtree can be replayed to a new parent
void acquire_descendant(response_t *child_response){
    command_code_t code = child_response->command_code;
    if(!((IS_LINK(code)) && LINK_SUBCOMMAND(code) == LINK_CHANGE_PARENT)){
        return;
    }
    if(child_response->response_code != OK || child_response->arguments[PARENT_ID_ARGUMENT] == id){
        return;
    }
    device_id_t descendant_id = child_response->source;
    device_type_t descendant_type = child_response->arguments[DEVICE_TYPE_ARGUMENT];
    device_id_t descendant_parent = child_response->arguments[PARENT_ID_ARGUMENT];

    if(!lock_data()){
        return;
    }
    error_code_t error_code = insert_indirect_routing_data(routing_table, descendant_id, descendant_type, descendant_parent);
    if(!IS_ERROR(error_code)){
        routing_data_t *descendant = find_routing_data(routing_table, descendant_id);
        if(descendant != NULL){
            update_type_to_not_empty(routing_table, descendant);
        }
        if(has_child){
            refresh_child_type(); //the child type changed with its branch, so the declared type follows it
        }
    }
    unlock_data();
    if(IS_ERROR(error_code) && error_code != ROUTE_NOT_FOUND){
        print_error(STDERR_FILENO, error_code, id, "while adding a descendant to the routing table");
    }
}

//a delete response means the device that sent it is gone, so it is released: the direct child frees the
//scalars and the declared type, a deeper descendant is only removed from the routing table, keeping the
//subtree replay from re-announcing dead nodes to a new parent
void release_child(response_t *child_response){
    if(!(IS_DELETE(child_response->command_code))){
        return;
    }
    if(!lock_data()){
        return;
    }
    if(has_child && child_response->source == child_id){
        error_code_t error_code = drop_child();
        if(IS_ERROR(error_code)){
            print_error(STDERR_FILENO, error_code, id, "while releasing the deleted child");
        }
    }
    else{
        routing_data_t *node = find_routing_data(routing_table, child_response->source);
        if(node != NULL){
            device_id_t descendant_parent = node->parent_id;
            remove_routing_data(routing_table, child_response->source, descendant_parent);
            routing_data_t *parent = find_routing_data(routing_table, descendant_parent);
            if(parent != NULL){
                update_type_to_empty(routing_table, parent);
            }
            if(has_child){
                refresh_child_type();
            }
        }
    }
    unlock_data();
}

//when a device is removed from a parent below the timer, that parent answers the remove-child request and its
//response travels up through here: it is the only notice that the device left the subtree, so the node is dropped,
//otherwise the subtree replay would announce to a new parent a device that is no longer below the timer, and
//the new parent would take over a device that belongs to somebody else
void release_removed_descendant(response_t *child_response){
    command_code_t code = child_response->command_code;
    if(!((IS_LINK(code)) && LINK_SUBCOMMAND(code) == LINK_REMOVE_CHILD)){
        return;
    }
    if(child_response->response_code != OK){
        return;
    }
    device_id_t removed_id = child_response->arguments[CHILD_ID_ARGUMENT];
    device_id_t removed_parent = child_response->source;

    if(!lock_data()){
        return;
    }
    routing_data_t *node = find_routing_data(routing_table, removed_id);
    if(node != NULL && node->parent_id == removed_parent){
        remove_routing_data(routing_table, removed_id, removed_parent);
        routing_data_t *parent = find_routing_data(routing_table, removed_parent);
        if(parent != NULL){
            update_type_to_empty(routing_table, parent);
        }
        if(has_child){
            refresh_child_type();
        }
    }
    unlock_data();
}

//forgets the child and goes back to the plain timer type, the caller must already hold the lock
error_code_t drop_child(){
    remove_routing_data(routing_table, child_id, id);
    error_code_t error_code = OK;
    if(close(snd_requests_child_fd) < 0){
        error_code = UNABLE_TO_CLOSE_PIPE;
    }
    has_child = false;
    device_type = TIMER_DEVICE;
    return error_code;
}

//bottom-up thread: reads the responses coming from the child and forwards them up to the parent
void *child_responses_routine(void *arg){
    (void)arg;
    char child_buffer[MAX_RESPONSE_SIZE];
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
        memcpy(parse_buffer, child_buffer, MAX_RESPONSE_SIZE);
        if(!IS_ERROR(parse_response(&child_response, parse_buffer, MAX_RESPONSE_SIZE))){
            acquire_child(&child_response);
            acquire_descendant(&child_response);
            release_child(&child_response);
            release_removed_descendant(&child_response);
            if(handle_own_reply(&child_response)){
                continue;
            }
        }
        write_to_parent(child_buffer, "while forwarding a child response");
    }
    return NULL;
}

//if the child response is the reply to a request the timer made for mirroring, it is turned into the timer own
//response (source set to the timer) and sent up, and true is returned so the caller does not forward it too
bool handle_own_reply(response_t *child_response){
    if(!lock_data()){
        return false;
    }
    bool ours = take_pending(child_response->command_code);
    if(ours && IS_INFO(child_response->command_code)){
        //a live state different from the last one the timer commanded means the child was switched manually,
        //so a manual override is reported instead of a definite state (the next command clears it on its own),
        //a control device child with nothing below answers with an undefined state, which is not a divergence
        //and is forwarded as it is
        if(child_response->arguments[STATE_ARGUMENT] != state
            && child_response->arguments[STATE_ARGUMENT] != UNDEFINED_STATE){
            child_response->arguments[STATE_ARGUMENT] = STATE_MANUAL_OVERRIDE;
        }
        child_response->arguments[BEGIN_ARGUMENT] = begin;
        child_response->arguments[END_ARGUMENT] = end;
    }
    unlock_data();
    if(!ours){
        return false;
    }

    child_response->source = id; //the parent gets the response from the timer, not from the child
    if(IS_SWITCH(child_response->command_code)){
        child_response->arguments_size = 0;
    }
    else if(IS_INFO(child_response->command_code)){
        //the child info reply already carries its live state and its on/open time in the first two positions,
        //which are kept as they are, so a parent reads them where every other device puts them, while begin and end
        //replace whatever the child put after them
        child_response->arguments_size = MAX_TIMER_ARGUMENTS;
    }
    else if(IS_DELETE(child_response->command_code)){
        child_response->arguments_size = 0;
    }

    char buffer[MAX_RESPONSE_SIZE];
    error_code_t error_code = format_response(child_response, buffer, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting the mirrored response");
        return true;
    }
    write_to_parent(buffer, "while sending the mirrored response");
    //the child confirmed its own deletion, so the branch below is gone and the timer terminates too: the
    //shutdown is performed here because the main thread is blocked reading the requests pipe
    if(IS_DELETE(child_response->command_code)){
        force_exit = true;
        handle_shutdown(OK);
    }
    return true;
}

//builds the switch command for the child from its type and sends it down, the label depends on the child
//type (power for a bulb, open or close for a window or a fridge), the position turns it on or off
error_code_t send_child_switch(bool activate){
    if(!lock_data()){
        return UNABLE_TO_LOCK_MUTEX;
    }
    bool present = has_child;
    device_id_t target = child_id;
    device_type_t type = child_type;
    int child_fd = snd_requests_child_fd;
    unlock_data();
    if(!present){
        return CHILD_NOT_FOUND;
    }

    command_code_t switch_code = SWITCH;
    if(IS_BULB_LIKE(type)){
        switch_code |= SWITCH_POWER | (activate ? POSITION_ON : POSITION_OFF);
    }
    else{
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
        release_unreachable_child();
        return UNABLE_TO_WRITE_PIPE;
    }
    return OK;
}

//schedule thread: sleeps until the next begin or end and switches the child on or off, repeating each day
void *schedule_routine(void *arg){
    (void)arg;
    while(!force_exit){
        if(!lock_data()){
            return NULL; //without the mutex the times can not be read, and retrying would spin printing errors
        }
        u_int16_t local_begin = begin;
        u_int16_t local_end = end;
        unlock_data();

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

        if(send_child_switch(activate) == OK && lock_data()){
            state = activate ? STATE_ON : STATE_OFF;
            unlock_data();
        }
    }
    return NULL;
}

error_code_t execute_command(){
    command_code_t code;
    response.command_code = NULL_COMMAND;
    response.arguments_size = 0;
    defer_response = false;

    error_code_t error_code = read_pipe();
    if(IS_ERROR(error_code)){
        response.response_code = error_code;
    }
    else if(request.destination != id) {
        if(!lock_data()){
            response.response_code = UNABLE_TO_LOCK_MUTEX;
        }
        else{
            bool forward = has_child;
            int child_fd = snd_requests_child_fd;
            unlock_data();
            if(forward){
                error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
                if(!IS_ERROR(forward_code) && write(child_fd, buffer_read, MAX_REQUEST_SIZE) == MAX_REQUEST_SIZE){
                    return error_code; //forwarded: the response is sent by the destination and not by the timer
                }
                if(!IS_ERROR(forward_code)){
                    release_unreachable_child();
                }
                response.response_code = UNABLE_TO_WRITE_PIPE;
            }
            else{
                response.response_code = DEVICE_NOT_FOUND;
            }
        }
    }
    else{
        code = request.command_code;
        response.command_code = code;
        response.response_code = OK;

        if(IS_INFO(code)) { create_info_response(); }
        else if(IS_LINK(code)) { create_link_response(); }
        else if(IS_REGISTRY(code)) { create_registry_response(); }
        else if((IS_SWITCH(code))) { create_switch_response(); }
        else if((IS_DELETE(code))) { create_delete_response(); }
        else{
            response.response_code = UNEXPECTED_COMMAND;
        }
    }

    if(!defer_response){
        simulate_processing_time();
        write_pipe();
    }

    //after the own change-parent response is sent, the branch is replayed so the new parent rebuilds it: a
    //control-device child has its whole subtree in the table, a leaf child (no descendants) is replayed alone
    if(parent_changed){
        bool control_child = false;
        if(lock_data()){
            control_child = has_child && (IS_CONTROL(child_type));
            unlock_data();
        }
        if(control_child){ replay_subtree(); }
        else{ replay_child_add(); }
        parent_changed = false;
    }

    return error_code;
}

//the child state can not be cached, so the info request is propagated to the child and its live state is
//mirrored when it replies (in the bottom-up thread), which is what makes a manual override on the child visible
void create_info_response(){
    //snapshot, pending and the write to the child share the same short lock: the write is non blocking (the
    //child reads its request immediately), so holding the lock across it does not serialize anything, the delay
    //(simulate_processing_time) is the only blocking step and stays outside the lock, in execute_command
    if(!lock_data()){
        response.response_code = UNABLE_TO_LOCK_MUTEX;
        return;
    }
    if(!has_child){
        response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
        response.arguments[ON_SECONDS_ARGUMENT] = 0;
        response.arguments[BEGIN_ARGUMENT] = begin;
        response.arguments[END_ARGUMENT] = end;
        response.arguments_size = MAX_TIMER_ARGUMENTS;
    }
    //the reply is registered before the request leaves, so it can never arrive before the timer can match it
    else if(!add_pending(request.command_code)){
        response.response_code = UNABLE_TO_ALLOCATE_HEAP;
    }
    else{
        request.destination = child_id;
        error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
        if(IS_ERROR(forward_code) || write(snd_requests_child_fd, buffer_read, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
            take_pending(request.command_code); //the request never left, so no reply has to be awaited for it
            if(!IS_ERROR(forward_code)){
                error_code_t drop_code = drop_child();
                if(IS_ERROR(drop_code)){
                    print_error(STDERR_FILENO, drop_code, id, "while releasing the unreachable child");
                }
            }
            response.response_code = UNABLE_TO_WRITE_PIPE;
        }
        else{
            defer_response = true;
        }
    }
    unlock_data();
}

void create_link_response(){
    if(LINK_SUBCOMMAND(request.command_code)==LINK_CHANGE_PARENT){
        device_id_t new_parent_id = request.argument;
        response.arguments[PARENT_ID_ARGUMENT] = new_parent_id;
        response.arguments_size = 2;
        //device_type is set by the child-responses thread and snd_responses_fd is written by it too, so reading
        //the type and re-pointing the pipe are both done under the same lock
        if(!lock_data()){
            response.response_code = UNABLE_TO_LOCK_MUTEX;
            return;
        }
        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type;
        if(parent_id!=new_parent_id){
            response.response_code = change_snd_responses_pipe(new_parent_id,&snd_responses_fd);
            if(response.response_code == OK){
                parent_id = new_parent_id;
                parent_changed = true;
            }
        }
        unlock_data();
    }
    else if(LINK_SUBCOMMAND(request.command_code)==LINK_REMOVE_CHILD){
        response.arguments[CHILD_ID_ARGUMENT] = request.argument;
        response.arguments_size = 2;
        if(!lock_data()){
            response.response_code = UNABLE_TO_LOCK_MUTEX;
            return;
        }
        if(has_child && request.argument == child_id){
            response.response_code = drop_child();
        }
        else{
            response.response_code = DEVICE_NOT_FOUND;
        }
        //losing the child changes the declared type, so the parent learns the new one from this response
        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type;
        unlock_data();
    }
    else{
        response.response_code = UNEXPECTED_COMMAND;
    }
}

//re-announces the child to the new parent by faking its change-parent response, so the new parent and the
//chain above rebuild the branch, the source is the child and the declared parent is the timer
void replay_child_add(){
    if(!lock_data()){
        return;
    }
    bool present = has_child;
    device_id_t replayed_child_id = child_id;
    device_type_t replayed_child_type = child_type;
    unlock_data();
    if(!present){
        return;
    }

    response_t child_add;
    child_add.source = replayed_child_id;
    child_add.command_code = LINK | LINK_CHANGE_PARENT; //looks like the child own change-parent response
    child_add.response_code = OK;
    child_add.arguments[PARENT_ID_ARGUMENT] = id;
    child_add.arguments[DEVICE_TYPE_ARGUMENT] = replayed_child_type;
    child_add.arguments_size = 2;

    char buffer[MAX_RESPONSE_SIZE];
    error_code_t error_code = format_response(&child_add, buffer, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting the child replay");
        return;
    }
    write_to_parent(buffer, "while sending the child replay");
}

//re-announces the whole tracked subtree to the new parent, one faked change-parent response per node, each with
//the node own parent and type, so the new parent and the chain above rebuild every branch, used when the child is
//a control device, find_all_routing_data returns a node always after its parent so the branch is rebuilt top-down
void replay_subtree(){
    response_t node_add;
    node_add.command_code = LINK | LINK_CHANGE_PARENT;
    node_add.response_code = OK;
    node_add.arguments_size = 2;
    char buffer[MAX_RESPONSE_SIZE];

    bool failed = false;
    if(!lock_data()){
        return;
    }
    routing_data_t *node = find_all_routing_data(routing_table, id, NULL);
    while(node != NULL && !failed){
        node_add.source = node->id;
        node_add.arguments[PARENT_ID_ARGUMENT] = node->parent_id;
        node_add.arguments[DEVICE_TYPE_ARGUMENT] = node->type;
        failed = IS_ERROR(format_response(&node_add, buffer, MAX_RESPONSE_SIZE))
            || write(snd_responses_fd, buffer, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE;
        node = find_all_routing_data(routing_table, id, node);
    }
    unlock_data();
    if(failed){
        print_error(STDERR_FILENO, BROKEN_PIPE, id, "while replaying the subtree");
        handle_shutdown(BROKEN_PIPE);
    }
}

//cancels the schedule thread waiting on the old times and starts a new one that uses the current begin and end
void reschedule(){
    if(schedule_thread_running){
        if(pthread_cancel(schedule_thread) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_CANCEL_THREAD, id, "while cancelling the schedule thread");
        }
        if(pthread_join(schedule_thread, NULL) != 0){
            print_error(STDERR_FILENO, UNABLE_TO_JOIN_THREAD, id, "while joining the schedule thread");
        }
        schedule_thread_running = false;
    }
    if(pthread_create(&schedule_thread, NULL, schedule_routine, NULL) != 0){
        print_error(STDERR_FILENO, UNABLE_TO_CREATE_THREAD, id, "while restarting the schedule thread");
        return;
    }
    schedule_thread_running = true;
}

//begin and end are minutes from midnight, and a timer does not run across midnight, so the only
//invalid case is begin > end, while begin == end is allowed
void create_registry_response(){
    response.arguments[REGISTRY_ARGUMENT] = request.argument;
    response.arguments_size = 1;
    bool changed = false;

    if(!lock_data()){
        response.response_code = UNABLE_TO_LOCK_MUTEX;
        return;
    }
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
    unlock_data();

    if(changed){
        reschedule();
    }
}

//the timer mirrors the state of its child, so a switch is propagated down, the state becomes consistent after
//the propagation, and the manual override is cleared because the mirrored state is set to the commanded one
void create_switch_response(){
    command_code_t code = request.command_code;
    if(!lock_data()){
        response.response_code = UNABLE_TO_LOCK_MUTEX;
        return;
    }
    bool valid = has_child && ((IS_BULB_LIKE(child_type) && SWITCH_LABEL(code)==SWITCH_POWER)
        || ((IS_WINDOW_LIKE(child_type) || IS_FRIDGE_LIKE(child_type))
            && (SWITCH_LABEL(code)==SWITCH_OPEN || SWITCH_LABEL(code)==SWITCH_CLOSE)));
    if(!has_child){
        response.response_code = DEVICE_NOT_FOUND;
    }
    else if(!valid){
        response.response_code = UNEXPECTED_COMMAND;
    }
    else if(!add_pending(code)){
        response.response_code = UNABLE_TO_ALLOCATE_HEAP;
    }
    else{
        request.destination = child_id;
        error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
        if(IS_ERROR(forward_code) || write(snd_requests_child_fd, buffer_read, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
            take_pending(code);
            if(!IS_ERROR(forward_code)){
                error_code_t drop_code = drop_child();
                if(IS_ERROR(drop_code)){
                    print_error(STDERR_FILENO, drop_code, id, "while releasing the unreachable child");
                }
            }
            response.response_code = UNABLE_TO_WRITE_PIPE;
        }
        else{
            if(SWITCH_POSITION(code)==POSITION_ON){
                state = (SWITCH_LABEL(code)==SWITCH_CLOSE) ? STATE_OFF : STATE_ON;
            }
            else if(SWITCH_LABEL(code)==SWITCH_POWER){
                state = STATE_OFF;
            }
            defer_response = true;
        }
    }
    unlock_data();
}

//deleting a control device terminates the branch below it, so the delete is propagated to the child and the
//timer answers only when the child confirms, then the shutdown is performed by the child-responses thread
void create_delete_response(){
    if(!lock_data()){
        response.response_code = UNABLE_TO_LOCK_MUTEX;
        return;
    }
    if(!has_child){
        force_exit = true; //nothing below to terminate, the response is sent and the main loop ends
    }
    else if(!add_pending(request.command_code)){
        response.response_code = UNABLE_TO_ALLOCATE_HEAP;
        force_exit = true;
    }
    else{
        request.destination = child_id;
        error_code_t forward_code = format_request(&request, buffer_read, MAX_REQUEST_SIZE);
        if(IS_ERROR(forward_code) || write(snd_requests_child_fd, buffer_read, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
            take_pending(request.command_code);
            response.response_code = UNABLE_TO_WRITE_PIPE;
            force_exit = true;
        }
        else{
            defer_response = true;
        }
    }
    unlock_data();
}

error_code_t open_child_requests_pipe(device_id_t child_id, int *snd_requests_child_fd){
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(child_id, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (*snd_requests_child_fd = open(name, O_WRONLY)) < 0){
        return UNABLE_TO_OPEN_PIPE;
    }
    return OK;
}
