#define _XOPEN_SOURCE 700

#include "hub.h"

// - Explicit control device data -

device_id_t id;
control_device_state_t state; //to be determined, based on the children type (that has to be the same)

//default it has to be hub
//when i add the first child i do (device_type || device_leaf_type)
device_type_t device_type; //to be determined at first add //TODO

// mutex needed
routing_table_t routing_table;
u_int32_t children;
linked_list_t *pending_requests = NULL;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;

// - IPC data -

int rcv_requests_parent_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_parent_fd; //write is non blocking  - pipe to send responses to parent or manual commands

//maybe the first write is blocking until one wants to read

int rcv_responses_children_fd; // all the children write on a same pipe 

bool force_exit = false; //only the main thread can modify it
bool eof = false;

// - Thread -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t children_thread;

//TODO when I have an end of file while reading i need to close the pipe and open another one
//because reading continuously EOF cause some problems

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler); //when a device write on a pipe but the device is no more listening due to crash or child removed

    id = get_id_from_arguments(argc, argv);
    start_device_fifos(id,&rcv_requests_parent_fd, &snd_responses_parent_fd, &rcv_responses_children_fd);
    srand(time(NULL));
    init_routing_table(&routing_table);
    device_type = HUB_DEVICE;

    error_code_t error_code;

    if(pthread_create(&children_thread, NULL, bottom_up_handler, NULL) < 0){
        handle_shutdown(UNABLE_TO_CREATE_THREAD);
    }
    else{
        //error_code = top_down_handler();
        //TODO
    }

    handle_shutdown(error_code);
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = OK;

    if(pthread_cancel(children_thread) != 0){
        error_code = UNABLE_TO_CANCEL_THREAD;
        print_error(STDERR_FILENO, error_code, id, "in shutdown");

    }
    if(children != 0){
        routing_data_t *current_child = find_direct_routing_data(routing_table, id, NULL);
        while(current_child != NULL){
            if(close(current_child->next_hop_fd) < 0){
                error_code = UNABLE_TO_CLOSE_PIPE;
                print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
                break;
            }
        }
        if(!IS_ERROR(error_code)){
            remove_routing_data(routing_table, id, parent_id);
        }
    }

    error_code = end_device_fifos(id, rcv_requests_parent_fd, snd_responses_parent_fd, rcv_responses_children_fd);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    else{ 
        error_code = error; 
    }

    exit(error_code);
}

void sigterm_handler(){
    handle_shutdown(UNEXPECTED_COMMAND);
}

void sigpipe_handler(){
    handle_shutdown(BROKEN_PIPE);
}

//TODO control
void bottom_up_handler(){
    //can be used to read the responses from the children and to write 
    //the responses up towards the parent of the hub
    char response_buffer[MAX_REQUEST_SIZE];
    response_t response;
    response.source = id;
    response.arguments_size = 0;

    bool is_parent = false;
    bool found, is_complete;
    
    linked_list_t found_request;
    error_code_t error_code; //to provide additional information, it's not the response_code

    while(!force_exit){
        command_code_t code;
        error_code_t error_code = read_pipe(is_parent, rcv_responses_children_fd, response_buffer, MAX_RESPONSE_SIZE, NULL, &response);

        if(IS_ERROR(error_code)){
            create_response(&response, NULL, error_code, 0);
            response.command_code = NULL_COMMAND;
        }
        else{
            response.source = id;
            //response.command_code --> not needed because it's already set in the response received
            if(IS_REGISTRY(response.command_code)){
                //forward up or check for manual override
            }
            else if(IS_LINK(response.command_code)){
                
            }
            else{
                //i need to check if it's a response that i was waiting for or not (forward up)
                response.response_code = check_pending_complete(&response, &found, &is_complete, &found_request);

                if(found && is_complete){
                    if(IS_INFO(code)) {info_response(&response, &found_request);}
                    else if(IS_SWITCH(code)) {switch_response(&response, &found_request);}
                    else if(IS_DELETE(code)) {delete_response(&response, &found_request);}
                }
            }
        }
            simulate_processing_time();
            write_pipe_response(&response, response_buffer);
    }
    //TODO pthread exit
}

//TODO if response type is control_device I need to check for the ADDITIONAL_INFO_ARGUMENT since the response_code is going to be OK
void info_response(response_t *response, linked_list_t *found_request){
    response->arguments[STATE_ARGUMENT] = found_request->state;
    response->arguments[OPEN_SECONDS_ARGUMENT] = found_request->max_time;
    response->arguments_size = 2;
    if(found_request->has_error){
        response->arguments[ADDITIONAL_INFO_ARGUMENT] = CHILD_ERROR; //TODO just a placeholder for now
        response->arguments_size = 3;
    }
}

void switch_response(response_t *response, linked_list_t *found_request){
    response->arguments_size = 0;
    if(found_request->has_error){
        response->arguments[ADDITIONAL_SWITCH_ARGUMENT] = CHILD_ERROR; //TODO just a placeholder for now
        response->arguments_size = 1;
    }
}

void delete_response(response_t *response, linked_list_t *found_request){
    if(found_request->has_error){
        response->arguments[ADDITIONAL_DELETE_ARGUMENT] = CHILD_ERROR; //TODO just a placeholder for now
        response->arguments_size = 1;
    }
}

//TODO take into account that a child can respond with an error_code
//if it's not i have to send it up
error_code_t check_pending_complete(response_t *response, bool *found, bool *is_complete, linked_list_t *found_request){ //TODO create those boolean in the main function
    if(pending_requests == NULL) return false;
    linked_list_t *current_pending = pending_requests;
    while(current_pending->next != NULL){
        if(current_pending->command_code == response->command_code){
            *is_complete = true;
            *found = false;
            for(u_int32_t i = 0; i < current_pending->requested_size; i++){
                if(current_pending->requested[i] != response->source && current_pending->requested[i] != NO_ID){
                    *is_complete = false;
                }
                else if(current_pending->requested[i] == response->source){ //founded
                    current_pending->requested[i] = NO_ID;

                    if(current_pending->state==NULL){
                        current_pending->state = response->arguments[STATE_ARGUMENT];
                    }
                    else if(current_pending->state != response->arguments[STATE_ARGUMENT]){
                            current_pending->state = STATE_MANUAL_OVERRIDE; //TODO control if it's correct
                    }

                    if(current_pending->max_time == 0){
                        current_pending->max_time = response->arguments[OPEN_SECONDS_ARGUMENT]; //or ON_SECONDS_ARGUMENT
                    }
                    else if(response->arguments[OPEN_SECONDS_ARGUMENT] > current_pending->max_time){
                        current_pending->max_time = response->arguments[OPEN_SECONDS_ARGUMENT];
                    }

                    if(IS_DELETE(response->command_code)){
                        error_code_t error_code = remove_child(response, NULL, response->source);
                        if(error_code == OK){
                            children--;
                        }
                        else{
                            current_pending->has_error = true;
                            return error_code;
                        }
                    }

                    if(IS_ERROR(response->response_code)){
                        current_pending->has_error = true;
                    }
                    else if(IS_CONTROL(response->arguments[DEVICE_TYPE_ARGUMENT])){
                        if(IS_INFO(response->command_code) && response->arguments[ADDITIONAL_INFO_ARGUMENT] == CHILD_ERROR){
                            current_pending->has_error = true;
                        }
                        else if(IS_SWITCH(response->command_code) && response->arguments[ADDITIONAL_SWITCH_ARGUMENT] == CHILD_ERROR){
                            current_pending->has_error = true;
                        }
                        else if(IS_DELETE(response->command_code) && response->arguments[ADDITIONAL_DELETE_ARGUMENT] == CHILD_ERROR){
                            current_pending->has_error = true;
                        }
                    }
                    *found = true;
                    *found_request = *current_pending;
                }
            }
            if(*found) return OK; //i have to exit if i found the id otherwise i would complete other requests with only one response
        }
        current_pending = current_pending->next;
    }
    return OK;
}


void top_down_handler(){
    //can be used to read the requests from the parent of the hub and to write
    //the requests to the children
    char request_buffer[MAX_REQUEST_SIZE];
    //can be used to write the response to the parent of the hub
    char response_buffer[MAX_RESPONSE_SIZE];

    request_t request;
    response_t response;
    command_code_t code;
    bool is_parent = true, has_children = false, is_request = false, has_parent_changed = false, is_forward_request = false;
    int child_fd;

    response.source = id;

    while(!force_exit){
        //read the request received from the parent
        error_code_t error_code = read_pipe(is_parent, rcv_requests_parent_fd, request_buffer, MAX_REQUEST_SIZE, &request, NULL);

        if(IS_ERROR(error_code)){
            response.command_code = NULL_COMMAND;
            create_response(&response, is_request, error_code, 0);
        }
        else{//no errors occurred
            code = request.command_code;
            response.command_code = code;
            
            if (has_children){
                if(request.destination == id){ //the destination is the parent
                    if(IS_INFO(code)) {forward_request(&request, is_forward_request, request_buffer);}
                    else if(IS_SWITCH(code)) { create_switch(&request, &response, &is_request, is_forward_request, request_buffer); }
                    else if(IS_LINK(code)) { create_link(&request, &response, &is_request, code, &has_parent_changed); }
                    else if(IS_DELETE(code)) { create_delete(&request, &response,is_forward_request, &is_request, request_buffer);}
                    else if(IS_REGISTRY(code)) { create_response(&response, &is_request, UNEXPECTED_COMMAND, 0); }
                }
                else{
                    send_to_child(&response, request.destination, &is_request, &child_fd);
                }
            }
            else{
                if(request.destination == id && IS_INFO(code)){
                    create_response(&response, is_request, OK, 1);
                    response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
                }
                else{
                    create_response(&response, is_request, CHILD_NOT_FOUND, 0);
                }
            }
        }
        //TODO
        if(!is_forward_request){
            simulate_processing_time();
            (is_request ? write_pipe_request(&request, request_buffer, child_fd) : write_pipe_response(&response, response_buffer));
        }

        if(has_parent_changed) {
            replay_history(&response, response_buffer);
            has_parent_changed = false;
        }
    }  
}

//TODO if i'm not the destination i need to modify these

//change parent
//remove child
void create_link(request_t *request, response_t *response, bool *is_request, command_code_t command_code,
                 bool *has_parent_changed){
    if(LINK_SUBCOMMAND(command_code)==LINK_REMOVE_CHILD){
        remove_child(response, command_code, request->argument);
    }
    else{
        u_int16_t new_parent_id = request->argument;
        error_code_t error_code = OK;
        response->arguments[PARENT_ID_ARGUMENT] = new_parent_id;
        response->arguments[DEVICE_TYPE_ARGUMENT] = device_type;

        if(parent_id != new_parent_id){
            error_code = change_snd_responses_pipe(new_parent_id,&snd_responses_parent_fd);
            if(error_code == OK){
                parent_id = new_parent_id;
                *has_parent_changed = true;
            }
        }
        create_response(response, is_request, error_code, 2);
    }
}

void send_to_child(response_t *response, device_id_t destination, bool *is_request, int *child_fd){
    routing_data_t *child = find_routing_data(routing_table, destination);
    if(child == NULL){
        create_response(&response, is_request, ROUTE_NOT_FOUND, 1);
        response->arguments[CHILD_ID_ARGUMENT] = destination;
    }
    else{
        is_request = true;
        *child_fd = child->next_hop_fd;
    }
}

void create_delete(request_t *request, response_t *response, bool *is_forward_request, bool *is_request, char* buffer_write){
    if(children > 0){
        forward_request(request, is_forward_request, buffer_write);
    }
    else{
        create_response(response,is_request, OK, 0);
        force_exit = true;
    }
}

//search if a child is in pending and i received a delete request of the child put it as complete


error_code_t remove_child(response_t *response, bool *is_request, device_id_t child_id){
    routing_data_t *child = find_routing_data(routing_table, child_id);
    if(response != NULL && children == 0){
        device_type = HUB_DEVICE;
        create_response(response, is_request, ROUTE_NOT_FOUND, 1); //TODO what i need to do if i have no children?
        response->arguments[DEVICE_TYPE_ARGUMENT] = device_type;
        response->arguments[CHILD_ID_ARGUMENT] = child_id;
    }
    if(child == NULL){
        if(response != NULL){
            create_response(&response, is_request, ROUTE_NOT_FOUND, 1);
            response->arguments[CHILD_ID_ARGUMENT] = child_id;
        }
        else{
            return ROUTE_NOT_FOUND;
        }
    }
    else if(close(child->next_hop_fd) < 0) {
        if(response != NULL){
            create_response(response, is_request, UNABLE_TO_CLOSE_PIPE, 0);
        }
        else{
            return UNABLE_TO_CLOSE_PIPE;
        }
    }
    remove_routing_data(routing_table, child->id, child->parent_id);
    return OK;
}

//the child ids are putted in the array and then when a child has responded its id is deleted from the array and set NO_ID
void forward_request(request_t *request, bool *is_forward_request, char* buffer_write){
    routing_data_t *direct_child = find_direct_routing_data(routing_table, id, NULL);
    linked_list_t *pending = malloc(sizeof(linked_list_t));
    u_int32_t i = 0;

    pending->has_error = false;
    pending->requested_size = children;
    pending->requested = malloc(sizeof(device_id_t)*pending->requested_size);
    *is_forward_request = true; //nothing to send

    while(direct_child != NULL) {
        request->destination = direct_child->id;
        simulate_processing_time();
        write_pipe_request(request, buffer_write, direct_child->next_hop_fd);
        pending->requested[i] = direct_child->id;
        direct_child = find_direct_routing_data(routing_table, id, direct_child);
        i++;
    }
    add_request(request->command_code, pending);
}

void replay_history(response_t *response, char *response_buffer){
    routing_data_t *next_child = find_all_routing_data(routing_table, id, NULL);
    while(next_child != NULL){
        response->arguments[PARENT_ID_ARGUMENT] = next_child->parent_id;
        response->source = next_child->id;
        response->arguments[DEVICE_TYPE_ARGUMENT] = next_child->type;
        response->arguments_size = 2;
        write_pipe_response(response, response_buffer);
        next_child = find_all_routing_data(routing_table, id, next_child);

    }
}

//it doesn't set the arguments
void create_response(response_t *response, bool *is_request, error_code_t error_code, size_t arguments_size){
    response->response_code = error_code;
    response->arguments_size = arguments_size;
    if(is_request!=NULL) is_request = false; //TODO see if this is useful or not
}

void add_request(command_code_t command_code, linked_list_t *pending){
    pending->command_code = command_code;
    pending->next = NULL;
    
    pending->max_time = 0;
    if(pending_requests == NULL) {
        pending_requests = pending;
        return;
    }
    linked_list_t *current = pending_requests;
    while(current->next != NULL) {
        current = current->next;
    }
    current->next = pending;
}

//TODO
/*
void critical_section_handler(int (*function)(int, char), response_t *response){
    if(pthread_mutex_lock(&data_mutex) < 0){
        response->response_code = UNABLE_TO_LOCK_MUTEX;
    }
    else{ function(); }
    if(pthread_mutex_unlock(&data_mutex) < 0){
        print_error(STDERR_FILENO, UNABLE_TO_UNLOCK_MUTEX, id, "while processing mutex unlock request");
    }
}
*/

void create_switch(request_t *request, response_t *response, bool *isrequest, bool* is_forward_request, char* buffer_write){
    if(((IS_BULB_LIKE(device_type) && SWITCH_LABEL(request->command_code)==SWITCH_POWER)) ||
        ((IS_FRIDGE_LIKE(device_type) || IS_WINDOW_LIKE(device_type)) &&
        (SWITCH_LABEL(request->command_code)==SWITCH_OPEN || SWITCH_LABEL(request->command_code)==SWITCH_CLOSE))){

        forward_request(request, is_forward_request, buffer_write);
    }
    else{
        create_response(response, isrequest, UNEXPECTED_COMMAND, 0);
    }
}

//EOF read --> no one can write anymore --> all the child have been linked to other parents
//if isRequest then response will be NULL
error_code_t read_pipe(bool is_parent, int fd, char* buffer, size_t buffer_size, request_t* request, response_t* response){
    ssize_t size = read(fd, buffer, buffer_size);
    if(size != buffer_size){
        return UNABLE_TO_READ_PIPE;
    }
    if(size == 0){
        if (is_parent){
            force_exit = true;
        }
        else{
            eof = true;
        }
        return UNEXPECTED_END_OF_FILE;
    }
    return (request==NULL ? parse_request(request, buffer, buffer_size) : parse_response(response, buffer, buffer_size));
}

void write_pipe_response(response_t* response, char* buffer_write){
    error_code_t error_code = format_response(response, buffer_write, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting response");
    }
    else if(write(snd_responses_parent_fd, buffer_write, MAX_RESPONSE_SIZE) != MAX_RESPONSE_SIZE ){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

void write_pipe_request(request_t* request, char* buffer_write, int snd_request_child_fd){
    error_code_t error_code = format_request(request, buffer_write, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting request");
    }
    else if(write(snd_request_child_fd, buffer_write, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending request");
    }
}


error_code_t close_fifos(bool is_children_EOF){
    error_code_t error_code = (is_children_EOF ? END_CHILDREN_FIFO : END_ALL_FIFOS); //TODO change this with if, not macro
    //TODO the function does not support no_file_descriptor, i need to create a function for it
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    return error_code;
}

//