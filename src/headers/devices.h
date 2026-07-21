/**
 * This file contains constant definitions and function declarations specific to device types, IDs and states
 */

#ifndef DOMOTICS_DEVICES_H
#define DOMOTICS_DEVICES_H

#include <stdbool.h>
#include <sys/types.h>

// Data types

typedef u_int16_t device_id_t;  // Limitation based on how arguments are formatted on messages
typedef u_int8_t device_type_t;

// Fixed IDS

#define NO_ID                 -2 // Special case, the ID is missing, used for error printing
#define MANUAL_INTERACTION_ID -1 // Special case, it is not really a device with an ID, used for error printing
#define CONTROLLER_ID         0

// Constants for device types

#define LEAF_DEVICE_MASK    0b0011
#define BULB_DEVICE         0b0001
#define WINDOW_DEVICE       0b0010
#define FRIDGE_DEVICE       0b0011
#define CONTROL_DEVICE_FLAG 0b1000
#define CONTROL_DEVICE_MASK 0b1100
#define HUB_DEVICE          0b1000
#define TIMER_DEVICE        0b1100

// Macros for type checking

#define IS_LEAF(t)        (t & CONTROL_DEVICE_FLAG) == 0b0000
#define IS_BULB(t)        (t == BULB_DEVICE)
#define IS_WINDOW(t)      (t == WINDOW_DEVICE)
#define IS_FRIDGE(t)      (t == FRIDGE_DEVICE)
#define IS_CONTROL(t)     (t & CONTROL_DEVICE_FLAG) == CONTROL_DEVICE_FLAG
#define IS_BULB_LIKE(t)   (t & LEAF_DEVICE_MASK) == BULB_DEVICE
#define IS_WINDOW_LIKE(t) (t & LEAF_DEVICE_MASK) == WINDOW_DEVICE
#define IS_FRIDGE_LIKE(t) (t & LEAF_DEVICE_MASK) == FRIDGE_DEVICE
#define IS_HUB(t)         (t & CONTROL_DEVICE_MASK) == HUB_DEVICE
#define IS_TIMER(t)       (t & CONTROL_DEVICE_MASK) == TIMER_DEVICE
#define IS_EMPTY(t)       (t & LEAF_DEVICE_MASK) == 0b0000
#define CHILD_TYPE(t)     (t & LEAF_DEVICE_MASK)

// Constants for device states

typedef bool leaf_device_state_t;
typedef u_int8_t control_device_state_t;

#define STATE_CLOSED          0
#define STATE_OFF             0
#define STATE_OPEN            1
#define STATE_ON              1
#define STATE_MANUAL_OVERRIDE 2
#define UNDEFINED_STATE       3

#define MIN_DELAY             5
#define MAX_DELAY             (3*60)
#define MIN_THERMOSTAT        2
#define MAX_THERMOSTAT        6
#define MAX_FILL_PERCENTAGE   100

// Constants

#define DEVICE_TYPE_SIZE  16
#define LEAF_TYPE_SIZE    8
#define CONTROL_TYPE_SIZE 8

/**
 * Formats device type to string for printing
 * 
 * @param type Device type to format
 * @param string Where to put the formatted device type, string of size `DEVICE_TYPE_SIZE`
 */
void format_device_type(device_type_t type, char string[DEVICE_TYPE_SIZE]);

#endif
