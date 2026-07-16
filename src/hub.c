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

int rcv_responses_children_fd; // all the children write on a same pipe 

//TODO
// but there is a pipe for every children

bool force_exit = false; //! only the main thread can modify this
bool eof = false; //! only the other thread can modify this
request_t request;
response_t response;


// - Signal handler -

struct sigaction action_handler;

// - Thread -
pthread_t children_execution_context;

//TODO when I have an end of file while reading i need to close the pipe and open another one
//because reading continuously EOF cause some problems

int main(int argc, char *argv[]) {

    bool is_children_EOF = false;

    id = get_id_from_arguments(argc, argv);
    response.source = id;
    start_device_fifos(id,&rcv_requests_parent_fd, &snd_responses_parent_fd, &rcv_responses_children_fd);
    action_handler.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &action_handler, NULL);

    //pthread_create(&children_communication_handler, NULL,  , NULL);

    //TODO after the thread creation i put the parent_communication_handler()
    //so the main thread will execute it
}

//TODO be careful about the global variables because i need a mutex or to make them local (inside a function)

//! the variables that i read only can be used by both threads without using a mutex

void children_communication_handler(){
    //can be used to read the responses from the children and to write 
    //the responses up towards the parent of the hub
    char response_buffer[MAX_REQUEST_SIZE];

    bool isParent = false;
    //TODO create a function when the code it's the same
    while(!force_exit){
        while(!eof){
            //-- similar code
            command_code_t code;
            response.arguments_size = 0;
            error_code_t error_code = read_pipe(isParent, rcv_responses_children_fd, response_buffer, MAX_RESPONSE_SIZE);
            //--
        }
        if(force_exit){
            //TODO close and open pipes
        }
    }
    //TODO pthread exit

}

void parent_communication_handler(){
    //can be used to read the requests from the parent of the hub and to write
    //the requests to the children
    char request_buffer[MAX_REQUEST_SIZE];
    //can be used to write the response to the parent of the hub
    char response_buffer[MAX_RESPONSE_SIZE];

    bool isParent = true;
    
    while(!force_exit){
        //-- similar code
        command_code_t code;
        response.arguments_size = 0;
        //TODO check if the function it's ok
        error_code_t error_code = read_pipe(isParent, rcv_requests_parent_fd, request_buffer, MAX_REQUEST_SIZE);
        //--
    }

}

//TODO put here the similar code, so it will be like the device
void execute_command(){

}

//EOF read --> no one can write anymore --> all the child have been linked to other partents
error_code_t read_pipe(bool isParent, int fd, char* buffer, int buffer_size){
    ssize_t size = read(fd, buffer, buffer_size);
    if(size < 0){
        return UNABLE_TO_READ_PIPE;
    }
    if(size == 0){
        if(isParent){
            force_exit = true;
            return UNEXPECTED_END_OF_FILE;
        }
        else{
            //TODO
            //eof
        }
    }
}

//TODO a boolean in the functions to identify if it's the parent or the child

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

//TODO only 2 threads in total, the main has to do the communication to/from the parent and the other to/from a child
