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
#include <ncurses.h>

#define INPUT_SIZE 64

int up;
int down;
device_id_t id;
WINDOW *responses_win;
WINDOW *commands_win;
int responses_height, commands_height, width;
int responses_bottom, commands_bottom;

void* read_thread(void *arg) {
    (void)arg; // Unused parameter
    char response_buffer[MAX_RESPONSE_SIZE];
    response_t response;
    bool loop = true;
    while(loop) {
        if(read(up, response_buffer, MAX_RESPONSE_SIZE) > 0) {
            wscrl(responses_win, 1);
            wmove(responses_win, responses_bottom, 0);
            wprintw(responses_win, "%s", response_buffer);
            //wmove(commands_win, height/2 -2, 1);
            
            wrefresh(responses_win);
            wrefresh(commands_win);
            
            parse_response(&response, response_buffer, MAX_RESPONSE_SIZE);
            if(response.response_code != OK) {
                //print_error(STDERR_FILENO, response.response_code, id, "in response");
            }
            if(IS_DELETE(response.command_code)) {
                loop = false;
            }
        } else {
            //print_error(STDERR_FILENO, UNABLE_TO_READ_PIPE, id, "in test");
        }
    }
    close(up);
    pthread_exit(NULL);
}

/**
 * Implements commands:
 * - switch <power/open/close> <on/off>
 * - link <parent_id>
 * - info
 * - del
 * - set <begin/end/delay/thermostat/percentage> <value>
 */
int main(int argc, char *argv[]) {
    id = get_id_from_arguments(argc, argv);
    char name_buffer[INPUT_SIZE];
    create_fifo_name(id, DIRECTION_DOWN, name_buffer, INPUT_SIZE);

    initscr();
    cbreak();

    int height, width;
    getmaxyx(stdscr, height, width);
    height -= 2; // 2 lines for the window titles
    responses_height = height / 2;
    commands_height = height - responses_height;
    responses_bottom = responses_height - 1;
    commands_bottom = commands_height - 1;

    mvwhline(stdscr, 0, 0, 0, width);
    mvwhline(stdscr, responses_height + 1, 0, 0, width);
    mvwprintw(stdscr, 0, 2, "Responses");
    mvwprintw(stdscr, responses_height + 1, 2, "Commands");

    responses_win = newwin(responses_height, width, 1, 0);
    commands_win = newwin(commands_height, width, responses_height + 2, 0);
    
    wmove(commands_win, commands_bottom, 0);
    scrollok(commands_win, true);
    scrollok(responses_win, true);
    refresh();
    wrefresh(responses_win);
    wrefresh(commands_win);

    down = open(name_buffer, O_WRONLY);
    up = open("./ipc/0_up.fifo", O_RDONLY);
    bool loop = true;
    //char *input_buffer = NULL;
    char input_buffer[64];
    size_t size = 64;
    char *token, *last;
    char request_buffer[MAX_REQUEST_SIZE];
    request_t request;
    request.destination = id;
    pthread_t tid;
    pthread_create(&tid, NULL, read_thread, NULL);
    while(loop) {
        wgetnstr(commands_win, input_buffer, size);
        wmove(commands_win, commands_bottom, 0);
        wrefresh(commands_win);
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

        // Send request
        format_request(&request, request_buffer, MAX_REQUEST_SIZE);
        write(down, request_buffer, MAX_REQUEST_SIZE);
        if(IS_LINK(request.command_code)) {
            close(up);
            create_fifo_name(request.argument, DIRECTION_UP, name_buffer, INPUT_SIZE);
            up = open(name_buffer, O_RDONLY);
        }
    }

    pthread_join(tid, NULL);
    close(down);
    endwin();
}