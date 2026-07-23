#define _XOPEN_SOURCE 700

#include "hub.h"
#include "stdio.h"

// - Explicit control device data -

device_id_t id;
device_type_t device_type; 

routing_table_t routing_table;
u_int32_t children;
bool has_children = false;
linked_list_t *pending_responses = NULL;

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;
volatile bool force_exit = false;

// - IPC data -

int rcv_requests_parent_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_parent_fd; //write is non blocking  - pipe to send responses to parent or manual commands
//maybe the first write is blocking until one wants to read

int rcv_responses_children_fd; // all the children write on a same pipe 

// - Thread -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t children_thread;

int main(int argc, char *argv[]) {
    set_signal_handler(SIGTERM, sigterm_handler);
    set_signal_handler(SIGPIPE, sigpipe_handler); //when a device write on a pipe but no device is listening anymore due to crash or child removed
    set_signal_handler(SIGINT, sigterm_handler);

    id = get_id_from_arguments(argc, argv);
    start_device_fifos(id,&rcv_requests_parent_fd, &snd_responses_parent_fd, &rcv_responses_children_fd);
    srand(time(NULL));
    init_routing_table(routing_table);
    device_type = HUB_DEVICE;

    error_code_t error_code = OK;

    if(pthread_create(&children_thread, NULL, bottom_up_handler, NULL) != 0){
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
    command_code_t code;
    error_code_t error_code = UNEXPECTED_SHUTDOWN;
    bool is_request = false;
    bool parent_changed = false;
    bool to_be_forwarded = false;
    int child_fd;

    response.source = id;

        while(!force_exit){
            is_request = false;
            to_be_forwarded = false;
            response.arguments_size = 0;

            error_code = read_pipe(rcv_requests_parent_fd, request_buffer, MAX_REQUEST_SIZE);
            if(!IS_ERROR(error_code)){
                error_code = parse_request(&request, request_buffer, MAX_REQUEST_SIZE);
            }
            if(IS_ERROR(error_code)){
                response.command_code = NULL_COMMAND;
                response.response_code = error_code;
            }
            else{//no errors occurred
                code = request.command_code;
                response.command_code = code;
                response.response_code = OK;

                //trying to get the mutex
                if(pthread_mutex_lock(&data_mutex) < 0){
                    error_code = UNABLE_TO_LOCK_MUTEX;
                    response.response_code = error_code;
                }
                else{
                    if (has_children){
                        if(request.destination == id){ //the destination is the parent
                            if(IS_INFO(code)) {
                                forward_request(&request, &response, &to_be_forwarded, request_buffer);
                            }
                            else if(IS_SWITCH(code)) { create_switch(&request, &response, &to_be_forwarded, request_buffer); }
                            else if(IS_LINK(code)) { create_link(&request, &response, &parent_changed); }
                            else if(IS_DELETE(code)) { forward_request(&request, &response, &to_be_forwarded, request_buffer); }
                            else{
                                response.response_code = UNEXPECTED_COMMAND;
                            }
                        }
                        else{
                            int temp = send_to_child(&response, request.destination);
                            if(temp >= 0){
                                child_fd = temp;
                                is_request = true;
                            }
                        }
                    }
                    else{
                        if(request.destination == id){
                            if(IS_INFO(code)){
                                response.arguments_size = 2;
                                response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
                                response.arguments[OPEN_SECONDS_ARGUMENT] = 0;
                            }
                            else if(IS_DELETE(code)){
                                force_exit = true;
                            }
                            else{
                                response.response_code = UNEXPECTED_COMMAND;
                            }
                        }
                        else{
                            response.response_code = CHILD_NOT_FOUND;
                        }
                    }
                    if(pthread_mutex_unlock(&data_mutex) < 0){
                        error_code = UNABLE_TO_UNLOCK_MUTEX;
                        response.response_code = error_code;
                        force_exit = true;
                    }
                }                

                if(!to_be_forwarded){
                    simulate_processing_time();
                    (is_request ? write_pipe_request(&request, request_buffer, child_fd) : write_pipe_response(&response, response_buffer));
                }

                if(pthread_mutex_lock(&data_mutex) < 0){
                    error_code = UNABLE_TO_LOCK_MUTEX;
                    response.response_code = error_code;
                }
                else {
                    if (parent_changed) {
                        replay_history(&response, response_buffer);
                        parent_changed = false;
                    }
                    if(pthread_mutex_unlock(&data_mutex) < 0){
                        error_code = UNABLE_TO_UNLOCK_MUTEX;
                        response.response_code = error_code;
                        write_pipe_response(&response, response_buffer);
                        force_exit = true;
                    }
                }
            }  
        }
    return error_code;
}

void* bottom_up_handler(void* arg){
    (void)arg; //to avoid a warning

    //can be used to read the responses from the children and to write 
    //the responses up towards the parent of the hub
    char response_buffer[MAX_RESPONSE_SIZE];
    response_t response;
    command_code_t code;
    error_code_t error_code;

    linked_list_t *solved_response = NULL;
    linked_list_t *previous_response = NULL;

    bool found, is_complete;

    while(!force_exit){

        found = false;
        is_complete = false;

        error_code = read_pipe(rcv_responses_children_fd, response_buffer, MAX_RESPONSE_SIZE);
        if(!IS_ERROR(error_code)){
            error_code = parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        }
        if(IS_ERROR(error_code)){
            response.command_code = NULL_COMMAND;
            response.response_code = error_code;
        }
        else if(!IS_ERROR(response.response_code)){ //if the response has errors then it's just forwarded
            code = response.command_code;

            if(IS_LINK(code)){
                if(pthread_mutex_lock(&data_mutex) < 0){
                    response.response_code = UNABLE_TO_LOCK_MUTEX;
                    
                }
                else{
                    link_response(&response);
                    if(pthread_mutex_unlock(&data_mutex) < 0){
                        print_error(STDERR_FILENO, UNABLE_TO_UNLOCK_MUTEX, id, "while processing mutex unlock request");
                        force_exit = true;
                    }
                }
            }
            else{
                //i need to check if it's a response that i was waiting for or not (forward up)
                if(pthread_mutex_lock(&data_mutex) < 0){
                    response.response_code = UNABLE_TO_LOCK_MUTEX;
                }
                else{                     
                    //double pointers
                    check_pending_complete(&response, &found, &is_complete, &solved_response, &previous_response);
                    if(pthread_mutex_unlock(&data_mutex) < 0){
                        response.response_code = UNABLE_TO_UNLOCK_MUTEX;
                        force_exit = true;
                    }       
                }         
                if(found && is_complete && !force_exit){
                    if(IS_INFO(code)) {info_response(&response, solved_response);}
                    else if(IS_SWITCH(code)) {switch_response(&response, solved_response);}
                    else if(IS_DELETE(code)) {
                        delete_response(&response, solved_response);
                        force_exit = true;
                    }
                    response.source = id;

                    if(solved_response == pending_responses){
                        pending_responses = solved_response->next;
                    }
                    else if(previous_response != NULL){
                        previous_response->next = solved_response->next;
                    }
                    free(solved_response->pending_devices);
                    free(solved_response);
                }
                //if i receive a delete response from a child I need to remove it from the routing table and close its pipe
                else if(!force_exit && IS_DELETE(code) && !found){
                    error_code = link_remove_child(response.source);
                    if(IS_ERROR(error_code)) print_error(STDERR_FILENO, error_code, id, "while closing the child pipe");
                }
            }
        }
        if(!(found && !is_complete)){
            simulate_processing_time();
            write_pipe_response(&response, response_buffer);
        }
    }
    pthread_exit(NULL);
    return NULL;
}

void handle_shutdown(error_code_t error) {
    error_code_t error_code = OK;
    device_id_t current_child_id;

    if(pthread_cancel(children_thread) != 0){
        error_code = UNABLE_TO_CANCEL_THREAD;
        print_error(STDERR_FILENO, error_code, id, "in shutdown");
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

void sigpipe_handler(int sig_num){
    (void)sig_num;
    handle_shutdown(BROKEN_PIPE);
}

//TODO .h
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
        response->response_code = link_remove_child_received(response->arguments[CHILD_ID_ARGUMENT]);
    }
    else{
        add_child(response);
    }
}

void add_child(response_t *response){
    error_code_t error_code = OK;
    int child_fd;
    //direct child
    if(response->arguments[PARENT_ID_ARGUMENT] == id){
        error_code = open_pipe(response->source, &child_fd);
        if(!IS_ERROR(error_code)){
            error_code = insert_direct_routing_data(routing_table, response->source, response->arguments[DEVICE_TYPE_ARGUMENT], id, child_fd);
            if(!has_children) {
                has_children = true;
                device_type = HUB_DEVICE | response->arguments[DEVICE_TYPE_ARGUMENT];
            }
            children++;
        }
    }
    else{
        error_code = insert_indirect_routing_data(routing_table, response->source, response->arguments[DEVICE_TYPE_ARGUMENT], response->arguments[PARENT_ID_ARGUMENT]);
    }
    response->response_code = error_code;
}

void info_response(response_t *response, linked_list_t *solved_response){
    response->arguments[STATE_ARGUMENT] = solved_response->state;
    response->arguments[OPEN_SECONDS_ARGUMENT] = solved_response->max_time;
    response->arguments_size = 2;
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_INFO_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 3;
    }
}

void switch_response(response_t *response, linked_list_t *solved_response){
    response->arguments_size = 0;
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_SWITCH_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 1;
    }
}

void delete_response(response_t *response, linked_list_t *solved_response){
    if(solved_response->has_error){
        response->arguments[ADDITIONAL_DELETE_ARGUMENT] = CHILD_ERROR;
        response->arguments_size = 1;
    }
}

void check_pending_complete(response_t *response, bool *found, bool *is_complete, linked_list_t **solved_response, linked_list_t **previous_response){
    //no pending responses
    if(pending_responses == NULL) return;
    linked_list_t *current_pending = pending_responses;
    error_code_t error_code = OK;
    *previous_response = NULL;
    *solved_response = NULL;

    while(current_pending != NULL){
        *is_complete = true;
        *found = false;
        if(current_pending->command_code == response->command_code){
            for(u_int32_t i = 0; i < current_pending->pending_devices_size; i++){
                //there is one or more that are missed
                if((current_pending->pending_devices[i] != response->source) && (current_pending->pending_devices[i] != NO_ID)){
                    *is_complete = false;
                }
                //founded
                else if(current_pending->pending_devices[i] == response->source){
                    current_pending->pending_devices[i] = NO_ID; //set it as found
                    //if it's the first that arrives it sets the state as the one founded
                    if(current_pending->state==UNDEFINED_STATE){
                        current_pending->state = response->arguments[STATE_ARGUMENT];
                    }
                    //the state founded it's different from the others --> manual override
                    else if(current_pending->state != response->arguments[STATE_ARGUMENT] && current_pending->state != STATE_MANUAL_OVERRIDE){
                            current_pending->state = STATE_MANUAL_OVERRIDE;
                    }
                    //updating the max_time
                    if(response->arguments[OPEN_SECONDS_ARGUMENT] > current_pending->max_time){
                        current_pending->max_time = response->arguments[OPEN_SECONDS_ARGUMENT];
                    }
                    if(IS_DELETE(response->command_code)){
                       error_code = link_remove_child(response->source);
                        if(IS_ERROR(error_code)) current_pending->has_error = true;
                    }
                    //if it's a control device the response code can be OK while the additional argument can provide an error
                    if(IS_CONTROL(response->arguments[DEVICE_TYPE_ARGUMENT])){
                        if((IS_INFO(response->command_code) &&  response->arguments_size == 3 && response->arguments[ADDITIONAL_INFO_ARGUMENT] == CHILD_ERROR) || 
                           (IS_SWITCH(response->command_code) && response->arguments_size == 1 && response->arguments[ADDITIONAL_SWITCH_ARGUMENT] == CHILD_ERROR) ||
                           (IS_DELETE(response->command_code) && response->arguments_size == 1 && response->arguments[ADDITIONAL_DELETE_ARGUMENT] == CHILD_ERROR)){
                            current_pending->has_error = true;
                        }
                    }
                    *found = true;
                    *solved_response = current_pending; //TODO togliere dai pending quando ricevo una risposta di delete
                }
            }
            response->response_code = error_code;
            if(*found) return; //i have to exit if i found the id otherwise i would complete other requests with only one response
        }
        *previous_response = current_pending;
        current_pending = current_pending->next;
    }
}

//TODO
//empty hub --> empty timer not ok
//empty timer --> empty hub ok
//empty hub --> empty hub not ok
//TODO double removal

void create_link(request_t *request, response_t *response, bool *parent_changed){
    if(LINK_SUBCOMMAND(request->command_code)==LINK_REMOVE_CHILD){
        response->response_code = link_remove_child(request->argument);
        response->arguments[CHILD_ID_ARGUMENT] = request->argument;
        response->arguments_size = 1;
    }   
    else{
        response->response_code = link_change_parent(request, response, parent_changed);
    }
}

error_code_t link_change_parent(request_t *request, response_t *response, bool *parent_changed){
    error_code_t error_code = OK;
    u_int16_t new_parent_id = request->argument;
    response->arguments[PARENT_ID_ARGUMENT] = new_parent_id;
    response->arguments[DEVICE_TYPE_ARGUMENT] = device_type;
    response->arguments_size = 2;
    
    if(parent_id != new_parent_id){
        error_code = change_snd_responses_pipe(new_parent_id, &snd_responses_parent_fd);
        if(!IS_ERROR(error_code)){
            parent_id = new_parent_id;
            *parent_changed = true;
        }
    }
    return error_code;
}

int send_to_child(response_t *response, device_id_t destination){
    routing_data_t *routing_information = find_routing_data(routing_table, destination);
    if(routing_information == NULL){
        response->response_code = ROUTE_NOT_FOUND;
        response->arguments_size = 1;
        response->arguments[CHILD_ID_ARGUMENT] = destination;
        return -1;
    }
    return routing_information->next_hop_fd;
}

error_code_t link_remove_child(device_id_t child_id){
    error_code_t error_code = OK;
    dprintf(STDERR_FILENO, "%d id: %d\n", child_id, id);
    routing_data_t *routing_information = find_direct_child(child_id);
    if(children == 0 || routing_information == NULL){
        error_code = CHILD_NOT_FOUND;
    }
    else if(routing_information != NULL){
        error_code = close_pipe(routing_information->next_hop_fd);
    }
    
    if(!IS_ERROR(error_code)) {
        remove_routing_data(routing_table, child_id, routing_information->parent_id);
        children--;
        if(children == 0) {
            has_children = false;
            device_type = HUB_DEVICE;
        }
    }
    return error_code;
}

error_code_t close_pipe(int fd){
    return (close(fd) < 0 ? UNABLE_TO_CLOSE_PIPE : OK);
}

routing_data_t* find_direct_child(device_id_t child_id){
    routing_data_t *child = find_direct_routing_data(routing_table, id, NULL);
    while(child != NULL){
        if(child->id == child_id){
            return child;
        }
        child = find_direct_routing_data(routing_table, id, child);
    }
    return NULL;
}

error_code_t link_remove_child_received(device_id_t child_id){
    error_code_t error_code = OK;
    routing_data_t *routing_information = find_routing_data(routing_table,child_id);
    if(routing_information == NULL){
        error_code = ROUTE_NOT_FOUND;
    }
    else{
        remove_routing_data(routing_table, child_id, routing_information->parent_id);
    }
    return error_code;
}

void forward_request(request_t *request, response_t *response, bool *to_be_forwarded, char* buffer_write){
    routing_data_t *direct_child = find_direct_routing_data(routing_table, id, NULL);
    linked_list_t *pending = init_pending_requests(request->command_code, response);
    if(pending == NULL) return;

    u_int32_t i = 0;
    
    *to_be_forwarded = true; //nothing to send

    while(direct_child != NULL) {
        request->destination = direct_child->id;
        write_pipe_request(request, buffer_write, direct_child->next_hop_fd);
        pending->pending_devices[i] = direct_child->id;
        direct_child = find_direct_routing_data(routing_table, id, direct_child);
        i++;
    }
    add_request(pending);
}

linked_list_t* init_pending_requests(command_code_t command_code, response_t *response){
    linked_list_t *pending = malloc(sizeof(linked_list_t));
    if(pending == NULL){
        response->response_code = UNABLE_TO_ALLOCATE_HEAP;
        return NULL;
    }
    pending->pending_devices_size = children;
    pending->pending_devices = malloc(sizeof(device_id_t)*pending->pending_devices_size);
    if(pending->pending_devices == NULL){
        response->response_code = UNABLE_TO_ALLOCATE_HEAP;
        return NULL;
    }
    pending->command_code = command_code;
    pending->next = NULL;
    pending->max_time = 0;
    pending->has_error = false;
    pending->state = UNDEFINED_STATE;
    return pending;
}

void replay_history(response_t *response, char *response_buffer){
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

void add_request(linked_list_t *pending){
    if(pending_responses == NULL) {
        pending_responses = pending;
        return;
    }
    linked_list_t *current = pending_responses;
    while(current->next != NULL) {
        current = current->next;
    }
    current->next = pending;
}

void create_switch(request_t *request, response_t *response, bool* to_be_forwarded, char* buffer_write){
    if(((IS_BULB_LIKE(device_type) && SWITCH_LABEL(request->command_code)==SWITCH_POWER)) ||
        ((IS_FRIDGE_LIKE(device_type) || IS_WINDOW_LIKE(device_type)) &&
        (SWITCH_LABEL(request->command_code)==SWITCH_OPEN || SWITCH_LABEL(request->command_code)==SWITCH_CLOSE))){

        forward_request(request, response, to_be_forwarded, buffer_write);
    }
    else{
        response->response_code = UNEXPECTED_COMMAND;
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