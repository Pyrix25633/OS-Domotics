#define _XOPEN_SOURCE 700

#include "hub.h"

// - Explicit control device data -

device_id_t id;
device_type_t device_type; 

routing_table_t routing_table;
u_int32_t children;
bool has_children = false;
pending_t *pending_responses = NULL;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
volatile bool force_exit = false; //the compiler does not optimize this variable (volatile)
bool is_bottom_up_thread = false;

// - IPC data -

int rcv_requests_parent_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_parent_fd; //write is non blocking  - pipe to send responses to parent or manual commands
//maybe the first write is blocking until one wants to read

int rcv_responses_children_fd; // all the children write on a same pipe 

// - Thread -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t bottom_up_thread;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, SIG_IGN); //when a device write on a pipe but no device is listening anymore due to crash or child removed
    set_signal_handler(SIGINT, sigterm_handler);

    id = get_id_from_arguments(argc, argv);
    start_device_fifos(id, &rcv_requests_parent_fd, &snd_responses_parent_fd, &rcv_responses_children_fd);
    srand(time(NULL));
    init_routing_table(routing_table);
    device_type = HUB_DEVICE;

    error_code_t error_code = OK;

    if(pthread_create(&bottom_up_thread, NULL, bottom_up_handler, NULL) != 0){
        error_code = UNABLE_TO_CREATE_THREAD;
    }
    else{
        error_code = top_down_handler();
    }
    handle_shutdown(error_code);
}

error_code_t top_down_handler(){
    //can be used to read the requests from the parent of the hub and to write
    //the requests to the children
    char request_buffer[MAX_REQUEST_SIZE];
    //can be used to write the response to the parent of the hub
    char response_buffer[MAX_RESPONSE_SIZE];

    request_t request;
    response_t response;
    error_code_t error_code = UNEXPECTED_SHUTDOWN;

    response.source = id;

    //it takes the last error
    while(!force_exit){
        response.arguments_size = 0;
        error_code = read_pipe(rcv_requests_parent_fd, request_buffer, MAX_REQUEST_SIZE);
        if(!IS_ERROR(error_code)){
            error_code = parse_request(&request, request_buffer, MAX_REQUEST_SIZE);
        }
        if(IS_ERROR(error_code)){
            response.command_code = NULL_COMMAND;
            response.response_code = error_code;
            simulate_processing_time();
            write_pipe_response(&response, response_buffer);
            continue;
        }
        //no errors occurred while reading the request
        command_code_t code = request.command_code;
        response.command_code = code;
        response.response_code = OK;

        //trying to get the mutex
        if(pthread_mutex_lock(&data_mutex) !=0){
            error_code = UNABLE_TO_LOCK_MUTEX;
            response.response_code = error_code;
            simulate_processing_time();
            write_pipe_response(&response, response_buffer);
            continue;
        }
        else{
            if(request.destination == id){
                if(has_children){
                    if(IS_INFO(code)){
                        error_code = forward_to_children(&request);                  
                        if(IS_ERROR(error_code)){
                            response.response_code = error_code;
                            simulate_processing_time();
                            write_pipe_response(&response, response_buffer);
                        }
                        if(pthread_mutex_unlock(&data_mutex) !=0){
                            error_code = UNABLE_TO_UNLOCK_MUTEX;
                            force_exit = true;
                        }     
                        continue;
                    }
                    if(IS_LINK(code)){
                        bool parent_changed = false;
                        response.source = id;
                        if(LINK_SUBCOMMAND(code)==LINK_CHANGE_PARENT){
                            response.arguments[PARENT_ID_ARGUMENT] = request.argument;
                            response.response_code = link_change_parent(request.argument, &parent_changed);
                        }
                        else if(LINK_SUBCOMMAND(code)==LINK_REMOVE_CHILD){
                            response.arguments[CHILD_ID_ARGUMENT] = request.argument;
                            response.response_code = remove_child(request.argument, id);
                        }
                        else{
                            response.response_code = UNEXPECTED_COMMAND;
                            simulate_processing_time();
                            write_pipe_response(&response, response_buffer);
                            if(pthread_mutex_unlock(&data_mutex) !=0){
                                error_code = UNABLE_TO_UNLOCK_MUTEX;
                                force_exit = true;
                            } 
                            continue;
                        }
                        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type;
                        response.arguments_size = 2;
                        simulate_processing_time();
                        write_pipe_response(&response, response_buffer);
                        if(parent_changed){
                            replay_history(&response);
                            response.source = id;
                        }
                        if(LINK_SUBCOMMAND(code)==LINK_REMOVE_CHILD) {
                            //in the check_pending I take the response source to check it because it 
                            //was in the bottom up but I need the child id (request argument) to be the source
                            response.source = request.argument;
                            check_complete_and_send(&response);
                        }
                        if(pthread_mutex_unlock(&data_mutex) !=0){
                            error_code = UNABLE_TO_UNLOCK_MUTEX;
                            force_exit = true;
                        }
                        continue;
                    }
                    if(IS_SWITCH(code)){
                        if(((IS_BULB_LIKE(device_type) && SWITCH_LABEL(code)==SWITCH_POWER)) ||
                            ((IS_FRIDGE_LIKE(device_type) || IS_WINDOW_LIKE(device_type)) &&
                            (SWITCH_LABEL(code)==SWITCH_OPEN || SWITCH_LABEL(code)==SWITCH_CLOSE))){

                            error_code_t error_code = forward_to_children(&request);

                            if(IS_ERROR(error_code)){
                                response.response_code = error_code;
                                simulate_processing_time();
                                write_pipe_response(&response, response_buffer);
                            }                            
                        }
                        else{
                            response.response_code = UNEXPECTED_COMMAND;
                            simulate_processing_time();
                            write_pipe_response(&response, response_buffer);
                        }
                        if(pthread_mutex_unlock(&data_mutex) !=0){
                            error_code = UNABLE_TO_UNLOCK_MUTEX;
                            force_exit = true;
                        }     
                        continue;
                    }
                    if(IS_DELETE(code)){
                        error_code = forward_to_children(&request);
                        if(IS_ERROR(error_code)){
                            response.response_code = error_code;
                            simulate_processing_time();
                            write_pipe_response(&response, response_buffer);
                            force_exit = true;
                        }
                        if(pthread_mutex_unlock(&data_mutex) !=0){
                            error_code = UNABLE_TO_UNLOCK_MUTEX;
                            force_exit = true;
                        }     
                        continue;
                    }//else
                    response.response_code = UNEXPECTED_COMMAND;
                    simulate_processing_time();
                    write_pipe_response(&response, response_buffer);

                    if(pthread_mutex_unlock(&data_mutex) !=0){
                        error_code = UNABLE_TO_UNLOCK_MUTEX;
                        force_exit = true;
                    }
                    continue;
                }
                else{//the hub doesn't have children
                    bool parent_changed = false;
                    if(IS_INFO(code)){
                        response.arguments_size = 2;
                        response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
                        response.arguments[OPEN_SECONDS_ARGUMENT] = 0;
                    }
                    else if(IS_DELETE(code)){
                        force_exit = true;
                    }
                    else if(IS_LINK(code) && LINK_SUBCOMMAND(code)==LINK_CHANGE_PARENT){
                        response.arguments[PARENT_ID_ARGUMENT] = request.argument;
                        response.response_code = link_change_parent(request.argument, &parent_changed);
                        response.arguments[DEVICE_TYPE_ARGUMENT] = device_type;
                        response.arguments_size = 2;
                    }
                    else{
                        response.response_code = UNEXPECTED_COMMAND;
                    }                        
                    simulate_processing_time();
                    write_pipe_response(&response, response_buffer);
                    if(parent_changed){
                        replay_history(&response);
                        response.source = id;
                    }
                    if(pthread_mutex_unlock(&data_mutex) !=0){
                        error_code = UNABLE_TO_UNLOCK_MUTEX;
                        force_exit = true;
                    }    
                    continue;
                }
            }
            else{
                routing_data_t *routing_information = find_routing_data(routing_table, request.destination);
                int destination = (routing_information == NULL ? NO_ROUTE : routing_information->next_hop_fd);
                if(pthread_mutex_unlock(&data_mutex) !=0){
                    error_code = UNABLE_TO_UNLOCK_MUTEX;
                    force_exit = true;
                    continue;
                }  
                error_code = send_to_child(&request, destination);
            }
        }                
    }
    return error_code;
}

error_code_t forward_to_children(request_t *request){
    char request_buffer[MAX_REQUEST_SIZE];

    routing_data_t *direct_child = find_direct_routing_data(routing_table, id, NULL);
    pending_t *pending = init_pending(request->command_code);
    if(pending == NULL) return UNABLE_TO_ALLOCATE_HEAP;
    //forward to all direct children
    u_int32_t i = 0;
    while(direct_child != NULL){
        request->destination = direct_child->id;
        write_pipe_request(request, request_buffer, direct_child->next_hop_fd);
        pending->pending_devices[i] = direct_child->id;
        i++;
        direct_child = find_direct_routing_data(routing_table, id, direct_child);
    }
    pending->pending_devices_size = i;
    //add the pending to the list
    if(pending_responses == NULL){
        pending_responses = pending;
        return OK;
    }
    pending_t *current = pending_responses;
    while(current->next != NULL){
        current = current->next;
    }
    current->next = pending;
    return OK;
}

pending_t* init_pending(command_code_t command_code){
    pending_t *pending = malloc(sizeof(pending_t));
    if(pending == NULL) return NULL;

    pending->pending_devices = malloc(sizeof(device_id_t)*children);
    if(pending->pending_devices == NULL) {
        free(pending);
        return NULL;
    }

    pending->command_code = command_code;
    pending->next = NULL;
    pending->max_time = 0;
    pending->has_error = false;
    pending->is_complete = false;
    pending->state = UNDEFINED_STATE;

    return pending;
}

error_code_t send_to_child(request_t *request, int next_hop_fd){
    error_code_t error_code = OK;
    response_t response;
    char request_buffer[MAX_REQUEST_SIZE];
    char response_buffer[MAX_RESPONSE_SIZE];
                
    if(next_hop_fd == NO_ROUTE){
        response.source = id;
        response.command_code = request->command_code;
        response.response_code = ROUTE_NOT_FOUND;
        error_code = ROUTE_NOT_FOUND;
        response.arguments_size = 1;
        response.arguments[CHILD_ID_ARGUMENT] = request->destination;
        simulate_processing_time();
        write_pipe_response(&response, response_buffer);
    }
    else{
        simulate_processing_time();
        write_pipe_request(request, request_buffer, next_hop_fd);
    }
    return error_code;
}

void* bottom_up_handler(void* arg){
    (void)arg; //to avoid a warning

    //can be used to read the responses from the children and to write 
    //the responses up towards the parent of the hub
    char response_buffer[MAX_RESPONSE_SIZE];
    response_t response;
    command_code_t code = NULL_COMMAND;
    error_code_t error_code = UNEXPECTED_SHUTDOWN;

    while(!force_exit){  
        error_code = read_pipe(rcv_responses_children_fd, response_buffer, MAX_RESPONSE_SIZE);
        if(!IS_ERROR(error_code)){
            error_code = parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        }
        if(pthread_mutex_lock(&data_mutex) !=0){
            response.response_code = UNABLE_TO_LOCK_MUTEX;
            response.command_code = NULL_COMMAND;
        }
        else{
            //if the response has errors then it's just forwarded
            if(IS_ERROR(error_code)){
                response.command_code = NULL_COMMAND;
                response.response_code = error_code;
                response.source = id;
                response.arguments_size = 0;
            }
            else{
                code = response.command_code;
                
                if(IS_LINK(code) && !IS_ERROR(response.response_code)){
                    link_response(&response);
                }
                else{
                    pending_t* previous = NULL;
                    pending_t* pending = check_pending(&response, &previous, false);

                    if(pending != NULL){
                        //the parameters of the pending are updated with the received response
                        update_pending(pending, &response);

                        if(pending->is_complete){
                            //the response is formatted based on the correct command code
                            format_response_type(&response, pending);
                            free_pending(&pending, previous);
                            write_pipe_response(&response, response_buffer);
                        }
                        if(pthread_mutex_unlock(&data_mutex) !=0){
                            force_exit = true;
                        }
                        continue;
                    }
                    else{
                        if(IS_DELETE(response.command_code)){
                            error_code = remove_child(response.source, NO_ID);
                            //i don't have to modify the response
                            if(IS_ERROR(error_code)){
                                print_error(STDERR_FILENO, error_code, id, "while closing the child pipe");
                            }
                            write_pipe_response(&response, response_buffer);
                            check_complete_and_send(&response);
                            if(pthread_mutex_unlock(&data_mutex) !=0){
                                force_exit = true;
                            }
                            continue;
                        }
                    }
                }
            }
            write_pipe_response(&response, response_buffer);
            if(pthread_mutex_unlock(&data_mutex) !=0){
                force_exit = true;
            }
        }
    }
    is_bottom_up_thread = true;
    handle_shutdown(OK);
    return NULL;
}

pending_t* check_pending(response_t* response, pending_t **previous, bool ignore_command){
    *previous = NULL;
    if(pending_responses == NULL) return NULL;

    pending_t *current_pending = pending_responses;
    bool found = false;

    while(current_pending != NULL){
        if(ignore_command || (current_pending->command_code == response->command_code)){
            current_pending->is_complete = true;
            for(u_int32_t i = 0; i < current_pending->pending_devices_size; i++){
                if(current_pending->pending_devices[i] == response->source){
                    current_pending->pending_devices[i] = NO_ID; //found
                    found = true;
                }
                else if(current_pending->pending_devices[i] != NO_ID){
                    current_pending->is_complete = false;
                }
            }
            if(found) return current_pending;
        }
        *previous = current_pending;
        current_pending = current_pending->next;
    }
    return NULL;
}

void update_pending(pending_t *pending, response_t *response){
    if(IS_INFO(response->command_code) && response->arguments_size >= 2){
        //state update
        if(pending->state == UNDEFINED_STATE){
            pending->state = response->arguments[STATE_ARGUMENT];
        }
        else if(pending->state != response->arguments[STATE_ARGUMENT] && response->arguments[STATE_ARGUMENT] != UNDEFINED_STATE){
            pending->state = STATE_MANUAL_OVERRIDE;
        }
        //max time update
        if(response->arguments[OPEN_SECONDS_ARGUMENT] > pending->max_time){
            pending->max_time = response->arguments[OPEN_SECONDS_ARGUMENT];
        }
    }
    //check for errors
    if(IS_ERROR(response->response_code)){
        pending->has_error = true;
    }

    routing_data_t *routing_information = find_routing_data(routing_table, response->source);
    //if it's a hub device the response code can be OK while the additional argument could provide an error
    if(routing_information != NULL && IS_HUB(routing_information->type)){
        if((IS_INFO(response->command_code) &&  response->arguments_size == 3 && response->arguments[ADDITIONAL_INFO_ARGUMENT] == CHILD_ERROR) || 
            (IS_SWITCH(response->command_code) && response->arguments_size == 1 && response->arguments[ADDITIONAL_SWITCH_ARGUMENT] == CHILD_ERROR) ||
            (IS_DELETE(response->command_code) && response->arguments_size == 1 && response->arguments[ADDITIONAL_DELETE_ARGUMENT] == CHILD_ERROR)){
            pending->has_error = true;
        }
    }
}

void format_response_type(response_t *response, pending_t *pending){
    response->source = id;
    command_code_t code = response->command_code;
    if(IS_INFO(code)) {
        info_response(response, pending);
    }
    else if(IS_SWITCH(code)) {switch_response(response, pending);}
    else if(IS_DELETE(code)) {
        delete_response(response, pending);
        force_exit = true;
    }
}

void check_complete_and_send(response_t *response){
    response_t child_response = *response;
    pending_t *previous = NULL;
    pending_t *pending = NULL;

    pending = check_pending(&child_response, &previous, true); 
    char response_buffer[MAX_RESPONSE_SIZE];
    while(pending != NULL){
        if(pending->is_complete){
            response->command_code = pending->command_code;
            format_response_type(response, pending);
            write_pipe_response(response, response_buffer);
            free_pending(&pending, previous);
        }
        pending = check_pending(&child_response, &previous, true);
    }                 
}

void free_pending(pending_t **pending, pending_t *previous){
    if(previous == NULL) pending_responses = (*pending)->next;
    else previous->next = (*pending)->next;

    free((*pending)->pending_devices);
    free(*pending);
    *pending = NULL;
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code  = OK;
    device_id_t current_child_id;

    if(!is_bottom_up_thread){
        if(pthread_cancel(bottom_up_thread) != 0) {
            error_code = UNABLE_TO_CANCEL_THREAD;
        }
        if(pthread_join(bottom_up_thread, NULL) != 0) {
            error_code = UNABLE_TO_JOIN_THREAD;
        }
        if(IS_ERROR(error_code)) {
            print_error(STDERR_FILENO, error_code, id, "while canceling bottom up thread");
        }
    }

    if(has_children){
        routing_data_t *current_child = find_direct_routing_data(routing_table, id, NULL);
        while(current_child != NULL){
            if(close(current_child->next_hop_fd) < 0){
                error_code = UNABLE_TO_CLOSE_PIPE;
                print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
                break;
            }
            current_child_id = current_child->id; //i need to save the id of the current child before updating it to remove it correctly after
            current_child = find_direct_routing_data(routing_table, id, current_child);
            remove_routing_data(routing_table, current_child_id, id);
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

void sigterm_handler(int sig_num){
    (void)sig_num;
    handle_shutdown(UNEXPECTED_SHUTDOWN);
}

error_code_t open_pipe(device_id_t device_id, int *snd_requests_child){
    char name[PIPE_NAME_MAX_LENGTH];
    if(IS_ERROR(create_fifo_name(device_id, DIRECTION_DOWN, name, PIPE_NAME_MAX_LENGTH))
        || (*snd_requests_child = open(name, O_WRONLY)) < 0) {
        return UNABLE_TO_OPEN_PIPE;
    }
    return OK;
}

void link_response(response_t *response){
    if(LINK_SUBCOMMAND(response->command_code)==LINK_REMOVE_CHILD){
        response->response_code = link_remove_child_received(response);
    }
    else{
        add_child(response);
    }
}

void add_child(response_t *response){
    error_code_t error_code = OK;
    error_code_t pipe_error = OK;
    int child_fd;
    //direct child
    if(response->arguments[PARENT_ID_ARGUMENT] == id){
        error_code = open_pipe(response->source, &child_fd);
        if(!IS_ERROR(error_code)){
            //if I already have the device id in my routing table I need to close the pipe before replacing the data
            routing_data_t *child = find_routing_data(routing_table, response->source);
            if(child != NULL && child->parent_id == id) pipe_error = close_pipe(child->next_hop_fd);
            error_code = insert_direct_routing_data(routing_table, response->source, response->arguments[DEVICE_TYPE_ARGUMENT], id, child_fd);
            if(device_type == HUB_DEVICE){
                if(!has_children) has_children = true;
                device_type = HUB_DEVICE | response->arguments[DEVICE_TYPE_ARGUMENT];
            }
            children++;
        }
    }
    else{
        error_code = insert_indirect_routing_data(routing_table, response->source, response->arguments[DEVICE_TYPE_ARGUMENT], response->arguments[PARENT_ID_ARGUMENT]);
        if(!IS_ERROR(error_code)){
            update_type_to_not_empty(routing_table, find_routing_data(routing_table, response->source));
            if(device_type == HUB_DEVICE){
                device_type = HUB_DEVICE | response->arguments[DEVICE_TYPE_ARGUMENT];
            }
        }
    }
    response->response_code = (pipe_error != OK && error_code == OK ? pipe_error : error_code);
}

void info_response(response_t *response, pending_t *solved_response){
    response->arguments[STATE_ARGUMENT] = solved_response->state;
    response->arguments[OPEN_SECONDS_ARGUMENT] = solved_response->max_time;
    response->arguments_size = 2;
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_INFO_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 3;
    }
}

void switch_response(response_t *response, pending_t *solved_response){
    response->arguments_size = 0;
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_SWITCH_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 1;
    }
}

void delete_response(response_t *response, pending_t *solved_response){
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_DELETE_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 1;
    }
}

error_code_t link_change_parent(device_id_t new_parent_id, bool *parent_changed){
    error_code_t error_code = OK;
    
    if(parent_id != new_parent_id){
        error_code = change_snd_responses_pipe(new_parent_id, &snd_responses_parent_fd);
        if(!IS_ERROR(error_code)){
            parent_id = new_parent_id;
            *parent_changed = true;
        }
    }
    return error_code;
}

error_code_t remove_child(device_id_t child_id, device_id_t parent_id){
    routing_data_t *child = find_routing_data(routing_table, child_id);
    if(child == NULL){
        return CHILD_NOT_FOUND;
    }
    if((parent_id == NO_ID ? child->parent_id : parent_id) == id){
        children--;
        if(children == 0) has_children = false;
    }
    device_id_t found_parent_id = child->parent_id;
    if(found_parent_id == id) close_pipe(child->next_hop_fd);
    remove_routing_data(routing_table, child_id, (parent_id == NO_ID ? child->parent_id : parent_id));
    //if the new parent stills one of my child I need to update it's type
    //otherwise I need to do it only upwards from its parent

    //but if it's a delete I need to do it because I receive a delete response from the child
    //and I can't separate the 2 things like with the link

    routing_data_t *parent = find_routing_data(routing_table, found_parent_id);

    if(parent_id == NO_ID || parent != NULL){
        update_type_to_empty(routing_table, parent);
    }
    update_type(child_id);
    return OK;
}

void update_type(device_id_t child_id){
    bool is_empty = true;
    routing_data_t *child = find_direct_routing_data(routing_table, id, NULL);
    while(child!=NULL){
        if(child->id == child_id) {
            child = find_direct_routing_data(routing_table, id, child);
            continue;
        }
        if(!IS_EMPTY(child->type)) {
            is_empty = false;
            break;
        }
        child = find_direct_routing_data(routing_table, id, child);
    }
    if(is_empty) device_type = HUB_DEVICE;
}

error_code_t close_pipe(int fd){
    return (close(fd) < 0 ? UNABLE_TO_CLOSE_PIPE : OK);
}

error_code_t link_remove_child_received(response_t* response){
    error_code_t error_code = OK;
    routing_data_t *routing_information = find_routing_data(routing_table,response->arguments[CHILD_ID_ARGUMENT]);
    if(routing_information == NULL){
        error_code = ROUTE_NOT_FOUND;
    }
    else{
        remove_routing_data(routing_table, response->arguments[CHILD_ID_ARGUMENT], response->source);
        //for all the hierarchy
        update_type_to_empty(routing_table, find_routing_data(routing_table, response->source));
        update_type(response->arguments[CHILD_ID_ARGUMENT]);
    }
    return error_code;
}

void replay_history(response_t *response){
    char response_buffer[MAX_RESPONSE_SIZE];
    routing_data_t *routing_information = find_all_routing_data(routing_table, id, NULL);
    while(routing_information != NULL){
        response->source = routing_information->id;
        response->arguments[PARENT_ID_ARGUMENT] = routing_information->parent_id;
        response->arguments[DEVICE_TYPE_ARGUMENT] = routing_information->type;
        response->arguments_size = 2;
        write_pipe_response(response, response_buffer);
        routing_information = find_all_routing_data(routing_table, id, routing_information);
    }
}

error_code_t read_pipe(int fd, char* buffer, size_t buffer_size){
    ssize_t size = read(fd, buffer, buffer_size);
    if(size == 0){
        force_exit = true;
        return UNEXPECTED_END_OF_FILE;
    }
    if (size != (ssize_t) buffer_size) return UNABLE_TO_READ_PIPE;

    return OK;
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

void write_pipe_request(request_t* request, char* buffer_write, int snd_request_fd){
    error_code_t error_code = format_request(request, buffer_write, MAX_REQUEST_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting request");
    }
    else if(write(snd_request_fd, buffer_write, MAX_REQUEST_SIZE) != MAX_REQUEST_SIZE){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending request");
    }
}