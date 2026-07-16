#define _XOPEN_SOURCE 700

#include "hub.h"

// - Explicit control device data -

device_id_t id;
control_device_state_t state; //to be determined, based on the children type (that has to be the same)

// - Auxiliary device data -

device_id_t parent_id = CONTROLLER_ID;

// - IPC data -

int rcv_requests_parent_fd;  //read is blocking       - pipe to receive requests from the parent or manual commands
int snd_responses_parent_fd; //write is non blocking  - pipe to send responses to parent or manual commands

//maybe the first write is blocking until one wants to read

int rcv_requests_children_fd; // all the children write on a same pipe 

//TODO
// but there is a pipe for every children

bool force_exit = false;
request_t request;
response_t response;
char buffer_read[MAX_REQUEST_SIZE];
char buffer_write[MAX_RESPONSE_SIZE];

// - Signal handler -

struct sigaction action_handler;

//TODO when I have an end of file while reading i need to close the pipe and open another one
//because reading continuously EOF cause some problems

int main(int argc, char *argv[]) {

    bool is_children_EOF = false;

    id = get_id_from_arguments(argc, argv);
    response.source = id;
    start_device_fifos(id,&rcv_requests_parent_fd, &snd_responses_parent_fd, rcv_requests_children_fd);
    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);

    while(!force_exit){
        //TODO
    } 
}

error_code_t close_fifos(bool is_children_EOF){
    error_code_t error_code = (is_children_EOF ? END_CHILDREN_FIFO : END_ALL_FIFOS); //TODO change this with if not macro
    //TODO the function does not support no_file_descriptor, i need to create a function for it
    if(error_code != OK){
        print_error(STDERR_FILENO, error_code, id, "while closing and deleting pipes");
    }
    return error_code;
}

//TODO a function is not needed, i need to separate close_fifos because i need to restart the pipes when EOF
void handle_shutdown(){
    exit(close_fifos(false));
}