#define _XOPEN_SOURCE 700

#include "devices.h"

#include <string.h>
#include <stdio.h>

void format_device_type(device_type_t type, char string[DEVICE_TYPE_SIZE]) {
    char leaf[LEAF_TYPE_SIZE];
    if(IS_BULB_LIKE(type)) {
        strcpy(leaf, "Bulb");
    }
    else if(IS_WINDOW_LIKE(type)) {
        strcpy(leaf, "Window");
    }
    else if(IS_FRIDGE_LIKE(type)) {
        strcpy(leaf, "Fridge");
    }
    else {
        strcpy(leaf, "Empty");
    }
    char control[CONTROL_TYPE_SIZE];
    if(IS_HUB(type)) {
        strcpy(control, "Hub");
    }
    else if(IS_TIMER(type)) {
        strcpy(control, "Timer");
    }
    if(IS_CONTROL(type)) {
        snprintf(string, DEVICE_TYPE_SIZE, "%s %s", leaf, control);
    }
    else {
        strcpy(string, leaf);
    }
}