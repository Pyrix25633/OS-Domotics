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

/**
 * Implements commands:
 * - switch <power/open/close> <on/off>
 * - link <parent_id>
 * - info
 * - del
 * - set <begin/end/delay/thermostat/percentage> <value>
 */
int main(int argc, char *argv[]) {
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
        input_buffer[strlen(input_buffer) - 1] = '\0'; // Remove '\n'

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
                request.command_code = LINK | LINK_CHANGE_PARENT;
                token = strtok_r(NULL, " ", &last);
                request.argument = string_to_unsigned(token);
            } else if(strcmp(token, "info") == 0) {
                request.command_code = INFO;
            } else if(strcmp(token, "del") == 0) {
                request.command_code = DELETE;
                loop = false;
            } else if(strcmp(token, "set") == 0) {
                request.command_code = REGISTRY;
                token = strtok_r(NULL, " ", &last);
                if(strcmp(token, "begin") == 0) {
                    request.command_code |= REGISTRY_BEGIN;
                } else if(strcmp(token, "end") == 0) {
                    request.command_code |= REGISTRY_END;
                } else if(strcmp(token, "delay") == 0) {
                    request.command_code |= REGISTRY_DELAY;
                } else if(strcmp(token, "thermostat") == 0) {
                    request.command_code |= REGISTRY_THERMOSTAT;
                } else if(strcmp(token, "percentage") == 0) {
                    request.command_code |= REGISTRY_PERCENTAGE;
                }
                token = strtok_r(NULL, " ", &last);
                request.argument = string_to_unsigned(token);
            }
        }

        free(input_buffer);
        input_buffer = NULL;

        // Send request
        format_request(&request, request_buffer, MAX_REQUEST_SIZE);
        printf("Request: %s\n", request_buffer);
        write(down, request_buffer, MAX_REQUEST_SIZE);
        if(IS_LINK(request.command_code)) {
            close(up);
            create_fifo_name(request.argument, DIRECTION_UP, name_buffer, INPUT_SIZE);
            up = open(name_buffer, O_RDONLY);
        }

        // Read response
        read(up, response_buffer, MAX_RESPONSE_SIZE);
        printf("Response: %s\n", response_buffer);
        parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
        if(response.response_code != OK) {
            print_error(STDERR_FILENO, response.response_code, id, "in response");
        }
    }

    close(up);
    close(down);
}