#define _XOPEN_SOURCE 700

#include "devices.h"
#include "utils.h"
#include "messages.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>

#define INPUT_SIZE 64

int main(int argc, char *argv[]) {
    /*
    
    sprintf(request_buffer, "1 72");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 21");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 72");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 24");
    write(down, request_buffer, REQUEST_SIZE);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);*/
    device_id_t id = get_id_from_arguments(argc, argv);
    char name_buffer[INPUT_SIZE];
    create_fifo_name(id, DIRECTION_DOWN, name_buffer, INPUT_SIZE);
    int down = open(name_buffer, O_WRONLY);
    int up = open("./ipc/0_up.fifo", O_RDONLY);
    bool loop = true;
    char *input_buffer = NULL;
    size_t size;
    char *token, *last;
    char request_buffer[MAX_REQUEST_SIZE];
    char response_buffer[MAX_RESPONSE_SIZE];
    request_t request;
    request.destination = id;
    response_t response;
    while(loop) {
        getline(&input_buffer, &size, stdin);
        input_buffer[strlen(input_buffer) - 1] = '\0';
        token = strtok_r(input_buffer, " ", &last);
        if(token != NULL) {
            if(strcmp(token, "switch") == 0) {
                request.command_code = SWITCH;
                token = strtok_r(NULL, " ", &last);
                if(strcmp(token, "power") == 0) {
                    request.command_code |= SWITCH_POWER;
                } else if(strcmp(token, "open") == 0) {
                    request.command_code |= SWITCH_OPEN;
                } else if(strcmp(token, "close") == 0) {
                    request.command_code |= SWITCH_CLOSE;
                }
                token = strtok_r(NULL, " ", &last);
                if(strcmp(token, "on") == 0) {
                    request.command_code |= POSITION_ON;
                } else if(strcmp(token, "off") == 0) {
                    request.command_code |= POSITION_OFF;
                }
            } else if(strcmp(token, "link") == 0) {
                request.command_code = LINK;
                token = strtok_r(NULL, " ", &last);
                request.argument = string_to_unsigned(token);
            } else if(strcmp(token, "info") == 0) {
                request.command_code = INFO;
            } else if(strcmp(token, "del") == 0) {
                request.command_code = DELETE;
            } else if(strcmp(token, "set") == 0) {
                request.command_code = REGISTRY;
                // TODO
            } else if(strcmp(token, "exit") == 0) {
                break;
            }
        }
        free(input_buffer); // ! some error here sometimes
        format_request(&request, request_buffer, MAX_REQUEST_SIZE);
        printf("Request: %s\n", request_buffer);
        write(down, request_buffer, MAX_REQUEST_SIZE);
        read(up, response_buffer, MAX_RESPONSE_SIZE);
        printf("Response: %s\n", response_buffer);
        parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        if(response.response_code != OK) {
            print_error(STDERR_FILENO, response.response_code, id, "in response");
        }
    }
}