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

// - Signal handler -

struct sigaction action_handler;

// - Thread -

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t children_execution_context;

//TODO when I have an end of file while reading i need to close the pipe and open another one
//because reading continuously EOF cause some problems

int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv);
    start_device_fifos(id,&rcv_requests_parent_fd, &snd_responses_parent_fd, &rcv_responses_children_fd);
    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);
    srand(time(NULL));
    init_routing_table(&routing_table);
    device_type = HUB_DEVICE;
    //pthread_create(&bottom_up_handler, NULL,  , NULL);

    //TODO after the thread creation i put the top_down_handler()
    //so the main thread will execute it
    //TODO handle_shutdown();
}

// the variables that i read only can be used by both threads without using a mutex

void bottom_up_handler(){
    //can be used to read the responses from the children and to write 
    //the responses up towards the parent of the hub
    char response_buffer[MAX_REQUEST_SIZE];
    response_t response;
    response.source = id;
    response.arguments_size = 0;

    bool is_parent = false;

    while(!force_exit){
        while(!eof){
            command_code_t code;
            error_code_t error_code = read_pipe(is_parent, rcv_responses_children_fd, response_buffer, MAX_RESPONSE_SIZE, NULL, &response);
            if(IS_ERROR(error_code)){
                response.command_code = NULL_COMMAND;
                response.response_code = error_code;
                simulate_processing_time();
                write_pipe_response(&response, response_buffer);
        }

        }
        if(force_exit){
            //TODO close and open pipes
        }
    }
    //TODO pthread exit

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

    response.source = id;

    while(!force_exit){
        // I need to send a request to the children based on the request done by the parent of the hub
        error_code_t error_code = read_pipe(is_parent, rcv_requests_parent_fd, request_buffer, MAX_REQUEST_SIZE, &request, NULL);
        // errors while parsing the request --> I need to send a response to the parent of the hub

        if(IS_ERROR(error_code)){
            response.command_code = NULL_COMMAND;
            create_response(&response, is_request, error_code, 0);
        }
        else{//no errors occurred
            if(request.destination == id){ //the destination is the parent
                code = request.command_code;
                response.command_code = code;
                if(has_children){ //TODO sistemare il fatto che controllo qui i children nelle varie funzioni
                    if(IS_INFO(code)) {forward_request(&request, is_forward_request, request_buffer);}
                    else if(IS_SWITCH(code)) { create_switch(&request, &response, &is_request, is_forward_request, request_buffer); }
                    else if(IS_LINK(code)) { create_link(&request, &response, &is_request, code, has_parent_changed); }
                    else if(IS_DELETE(code)) { create_delete(&request, &response, is_forward_request, is_request, request_buffer);}
                    else if(IS_REGISTRY(code)) { create_registry(&request, &response, &is_request, is_forward_request, request_buffer); }
                }
                else{
                    if(IS_INFO(code)){
                        create_response(&response, is_request, OK, 1);
                        response.arguments[STATE_ARGUMENT] = UNDEFINED_STATE;
                    }
                    else{
                        //create_response(response, is_request, CHILD_NOT_FOUND, 0); //TODO error_code
                    }
                }
            }
        }
        //TODO
        if(!is_forward_request){
            simulate_processing_time();
            //(is_request ? write_pipe_request() : write_pipe_response());
        }

        if(has_parent_changed) {
            replay_history(&response, response_buffer);
            has_parent_changed = false;
        }
    }
}

//TODO if i'm not the destination i need to modify these

//i need to send a request to every child for their info
void create_info(request_t *request, response_t *response, bool has_children, bool *is_request, bool* is_forward_request, char* buffer_write){
    forward_request(request, is_forward_request, buffer_write);
}

//change parent
//remove child
void create_link(request_t *request, response_t *response, bool *is_request, command_code_t command_code,
                 bool *has_parent_changed){
    if(LINK_SUBCOMMAND(command_code)==LINK_REMOVE_CHILD){
        remove_child(response,command_code, request->argument);
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

//pending
void create_delete(request_t *request, response_t *response, bool has_children, bool *is_forward_request, bool *is_request, char* buffer_write){
    if(has_children){
        routing_data_t *next_child = find_direct_routing_data(routing_table, id, NULL);
        while(next_child != NULL){
            forward_request(request, is_forward_request, buffer_write);
            next_child = find_all_routing_data(routing_table, id, next_child);
        }
    }
    else{
        //create_response(response, is_request, CHILD_NOT_FOUND, 0); //TODO error_code
    }
}

void remove_child(response_t *response, bool *is_request, device_id_t child_id){
    routing_data_t *child = find_routing_data(routing_table, child_id);
    if(close(child->next_hop_fd) < 0) {
        create_response(response, is_request, UNABLE_TO_CLOSE_PIPE, 0);
    }
    remove_routing_data(routing_table, child->id, child->parent_id);
    if(find_direct_routing_data(routing_table, id, NULL)==NULL){
        children = 0;
        device_type = HUB_DEVICE;
        create_response(response, is_request, OK, 1);
        response->arguments[DEVICE_TYPE_ARGUMENT] = device_type;
        response->arguments[CHILD_ID_ARGUMENT] = child_id;
    }
}

void forward_request(request_t *request, bool *is_forward_request, char* buffer_write){
    routing_data_t *direct_child = find_direct_routing_data(routing_table, id, NULL);
    children = 0;
    *is_forward_request = true; //nothing to send
    while(direct_child != NULL) {
        request->destination = direct_child->id;
        simulate_processing_time();
        write_pipe_request(request, buffer_write, direct_child->next_hop_fd);
        children++;
        direct_child = find_direct_routing_data(routing_table, id, direct_child);
    }
    add_request(request->command_code);
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
    is_request = false;
}

void add_request(command_code_t command_code){
    linked_list_t *pending = malloc(sizeof(linked_list_t));
    pending->command_code = command_code;
    pending->next = NULL;
    pending->responded_size = children;
    pending->responded = malloc(sizeof(device_id_t)*children);
    pending->max_time = 0;
    pending->responded_size = children;
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
    if(size < 0){
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
    else if(write(snd_responses_parent_fd, buffer_write, MAX_REQUEST_SIZE) < 0){
        print_error(STDERR_FILENO, UNABLE_TO_WRITE_PIPE, id, "while sending response");
    }
}

void write_pipe_request(request_t* request, char* buffer_write, int snd_request_child_fd){
    error_code_t error_code = format_request(request, buffer_write, MAX_RESPONSE_SIZE);
    if(IS_ERROR(error_code)){
        print_error(STDERR_FILENO, error_code, id, "while formatting request");
    }
    else if(write(snd_request_child_fd, buffer_write, MAX_REQUEST_SIZE) < 0){
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