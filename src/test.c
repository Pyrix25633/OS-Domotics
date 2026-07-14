#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>

#define REQUEST_SIZE 24
#define RESPONSE_SIZE 64

int main() {
    int down = open("./ipc/1_down.fifo", O_WRONLY);
    int up = open("./ipc/0_up.fifo", O_RDONLY);
    char request_buffer[REQUEST_SIZE];
    sprintf(request_buffer, "1 72");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 21");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 72");
    write(down, request_buffer, REQUEST_SIZE);
    sprintf(request_buffer, "1 24");
    write(down, request_buffer, REQUEST_SIZE);
    char response_buffer[RESPONSE_SIZE];
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
    read(up, request_buffer, RESPONSE_SIZE);
    printf("Received %s\n", request_buffer);
}