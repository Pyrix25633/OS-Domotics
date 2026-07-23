/**
 * This file contains constant definitions, type definitions and function declarations specific to routing
 */

#ifndef DOMOTICS_ROUTING_H
#define DOMOTICS_ROUTING_H

#include "devices.h"
#include "return_codes.h"

#include <sys/types.h>

// Hashing

#define ID_HASH_MASK     0xF // Takes the 4 least significant bits
#define UNIQUE_ID_HASHES ID_HASH_MASK + 1 // So 16 possible hashes
#define HASH_ID(i)       (i & ID_HASH_MASK)
#define GET_BUCKET(t, i) (t + HASH_ID(i))

#define NO_PID 0

typedef u_int8_t id_hash_t;
typedef u_int8_t depth_t;

typedef struct routing_data_t {
    device_id_t id;
    pid_t pid; // Used only in Controller
    device_type_t type;
    device_id_t parent_id;
    int next_hop_fd; // Set automatically by the functions
    struct routing_data_t *next; // Set automatically by the functions
} routing_data_t;

// Routing table, an array of all buckets, do not forget to call `init_routing_table`
typedef routing_data_t *routing_table_t[UNIQUE_ID_HASHES];

// Information sharing through file

#define REGISTRY_FILE      "./ipc/devices.registry"
#define TMP_REGISTRY_FILE  "./ipc/tmp.registry"
#define REGISTRY_LINE_SIZE 16

/**
 * Initializes the routing table by setting empty buckets
 */
void init_routing_table(routing_table_t table);

/**
 * Inserts, or replaces, routing data for a direct child, including PID
 * 
 * Automatically allocates needed space in the heap
 * 
 * @param table Routing table where to insert the data
 * @param id New device ID
 * @param pid New device PID
 * @param type Its type
 * @param current_id The current device ID, where the routing is performed
 * @param fd File descriptor where to send requests
 * 
 * @returns `UNABLE_TO_ALLOCATE_HEAP` if `malloc` failed,
 * `OK` otherwise
 */
error_code_t insert_direct_routing_data_pid(routing_table_t table, device_id_t id, pid_t pid, device_type_t type, device_id_t current_id, int fd);

/**
 * Inserts, or replaces, routing data for a direct child
 * 
 * Automatically allocates needed space in the heap
 * 
 * @param table Routing table where to insert the data
 * @param id New device ID
 * @param type Its type
 * @param current_id The current device ID, where the routing is performed
 * @param fd File descriptor where to send requests
 * 
 * @returns `UNABLE_TO_ALLOCATE_HEAP` if `malloc` failed,
 * `OK` otherwise
 */
error_code_t insert_direct_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t current_id, int fd);

/**
 * Inserts, or replaces, routing data for an indirect child
 * 
 * Automatically allocates needed space in the heap
 * 
 * @param table Routing table where to insert the data
 * @param id New device ID
 * @param pid New device PID
 * @param type Its type
 * @param parent_id Its parent ID
 * 
 * @returns `UNABLE_TO_ALLOCATE_HEAP` if `malloc` failed,
 * `ROUTE_NOT_FOUND` if a device with the specified parent ID was not found,
 * `OK` otherwise
 */
error_code_t insert_indirect_routing_data_pid(routing_table_t table, device_id_t id, pid_t pid, device_type_t type, device_id_t parent_id);

/**
 * Inserts, or replaces, routing data for an indirect child
 * 
 * Automatically allocates needed space in the heap
 * 
 * @param table Routing table where to insert the data
 * @param id New device ID
 * @param type Its type
 * @param parent_id Its parent ID
 * 
 * @returns `UNABLE_TO_ALLOCATE_HEAP` if `malloc` failed,
 * `ROUTE_NOT_FOUND` if a device with the specified parent ID was not found,
 * `OK` otherwise
 */
error_code_t insert_indirect_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t parent_id);

/**
 * Finds, if present, routing information for a specific device
 * 
 * @param table The routing table where to search
 * @param id The ID of the device to be found
 * 
 * @return A pointer to the data if found, `NULL` otherwise
 */
routing_data_t* find_routing_data(routing_table_t table, device_id_t id);

/**
 * Finds, if present, routing information about the next direct child
 * 
 * It has to be called multiple times until it returns `NULL`
 * 
 * @param table Routing table where to search
 * @param parent_id Parent ID of the searched direct child
 * @param last Pointer to the last returned child, where to start the search, if `NULL` the search starts from the beginning
 * 
 * @returns Each time it returns a pointer to the routing information of the next child, `NULL` if there are no more
 * direct children
 */
routing_data_t* find_direct_routing_data(routing_table_t table, device_id_t parent_id, routing_data_t *last);

/**
 * Finds, if present, routing information about the next child, not limited to direct childs
 * 
 * It has to be called multiple times until it returns `NULL`, the logical tree is traversed from
 * top to bottom, a child is always returned after its parent
 * 
 * This can be used to replay `CHANGE_PARENT` history
 * 
 * @param table Routing table where to search
 * @param parent_id Parent ID where the search starts, usually the top of the local table, ID of the device where
 * this operation is performed
 * @param last Pointer to the last returned child, where to start the search, if `NULL` the search starts from the beginning
 * 
 * @returns Each time it returns a pointer to the routing information of the next child, `NULL` if there are no more
 * direct children
 */
routing_data_t* find_all_routing_data(routing_table_t table, device_id_t parent_id, routing_data_t *last);

/**
 * Finds also routing data that is unreachable as the parent does not exist anymore,
 * used in the controller while handling `SIGCHLD`
 * 
 * @param table The routing table where to search
 * @param last Pointer to the last returned child, where to start the search, if `NULL` the search starts from the beginning
 * 
 * @returns Each time it returns a pointer to the routing information of the next element, `NULL` if there are no more elements
 */
routing_data_t* find_unreachable_routing_data(routing_table_t table, routing_data_t *last);

/**
 * Removes recursively, if present, the routing data, also about children of the removed ID,
 * only if the provided parent ID matches the actual parent ID found in the routing information
 * 
 * @param table The routing table the data has to be removed from
 * @param id ID of the device to be removed
 * @param parent_it Its expected parent ID
 */
void remove_routing_data(routing_table_t table, device_id_t id, device_id_t parent_id);

/**
 * Inserts, or replaces, the routing data ordered by ID
 * 
 * @param bucket Pointer to the bucked where to insert the data, linked list
 * @param data Data to be inserted
 */
void insert_routing_data_in_bucket(routing_data_t **bucket, routing_data_t *data);

/**
 * Removes, if present, the routing data
 * 
 * @param bucket Pointer to the bucket from which the data has to be removed
 * @param id ID of the device to be removed
 */
void remove_routing_data_from_bucket(routing_data_t **bucket, device_id_t id);

/**
 * Writes id and type to the registry file, so that it can be read by the manual interaction
 * 
 * @param table Routing table to be exported
 * @param parent_id Id of the parent which children, direct and indirect, should be exported
 * 
 * @returns `UNABLE_TO_OPEN_FILE` if the file could not be opened,
 * `UNABLE_TO_WRITE_FILE` if there was an error writing to the file,
 * `UNABLE_TO_CLOSE_FILE` if the file could not be opened,
 * `UNABLE_TO_RENAME_FILE` if the temporary file could not be renamed,
 * `OK` otherwise
 */
error_code_t export_routing_table(routing_table_t table, device_id_t parent_id);

/**
 * Finds the device type in the registry file, if present
 * 
 * @param id ID of the device which type has to be found
 * @param type Pointer where the found type will be put
 * 
 * @returns `UNABLE_TO_OPEN_FILE` if the file could not be opened and not because absent,
 * `REGISTRY_FORMAT_ERROR` if the registry file did not have the expected format,
 * `DEVICE_NOT_FOUND` if the registry file or the device ID were absent,
 * `OK` otherwise
 */
error_code_t find_device_type(device_id_t id, device_type_t* type);

#endif
