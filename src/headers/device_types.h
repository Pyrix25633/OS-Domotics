/**
 * This file contains constant definitions and function declarations specific to device types
 */

#ifndef DOMOTICS_DEVICE_TYPES_H
#define DOMOTICS_DEVICE_TYPES_H

// Data types
typedef unsigned device_id_t;
typedef unsigned char device_type_t;

// Constants
#define LEAF_DEVICE_MASK    0b0011
#define BULB                0b0001
#define WINDOW              0b0010
#define FRIDGE              0b0011
#define CONTROL_DEVICE_FLAG 0b1000
#define CONTROL_DEVICE_MASK 0b1100
#define HUB                 0b1000
#define TIMER               0b1100

// Macros for type checking
#define IS_LEAF(t)        (t & CONTROL_DEVICE_FLAG) == 0b0000
#define IS_BULB(t)        t == BULB
#define IS_WINDOW(t)      t == WINDOW
#define IS_FRIDGE(t)      t == FRIDGE
#define IS_CONTROL(t)     (t & CONTROL_DEVICE_FLAG) == CONTROL_DEVICE_FLAG
#define IS_BULB_LIKE(t)   (t & LEAF_DEVICE_MASK) == BULB
#define IS_WINDOW_LIKE(t) (t & LEAF_DEVICE_MASK) == WINDOW
#define IS_FRIDGE_LIKE(t) (t & LEAF_DEVICE_MASK) == FRIDGE
#define IS_HUB(t)         (t & CONTROL_DEVICE_MASK) == HUB
#define IS_TIMER(t)       (t & CONTROL_DEVICE_MASK) == TIMER
#define IS_EMPTY(t)       (t & LEAF_DEVICE_MASK) == 0b0000

#endif
