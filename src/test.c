#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

#define MAX 64

int main() {
    int down = open("./ipc/1_down.fifo", O_WRONLY);
    int up = open("./ipc/0_up.fifo", O_RDONLY);
    char buffer[MAX];
    sprintf(buffer, "1 72");
    write(down, buffer, MAX);
    sprintf(buffer, "1 24");
    write(down, buffer, MAX);
    read(up, buffer, MAX);
    printf("Received %s\n", buffer);
    read(up, buffer, MAX);
    printf("Received %s\n", buffer);
}