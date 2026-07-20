#define _XOPEN_SOURCE 700

#include "routing.h"

#include <stdlib.h>
#include <stdio.h> // ! remove

void init_routing_table(routing_table_t table) {
    for(id_hash_t b = 0; b < UNIQUE_ID_HASHES; b++) {
        table[b] = NULL;
    }
}

error_code_t insert_direct_routing_data_pid(routing_table_t table, device_id_t id, pid_t pid, device_type_t type, device_id_t current_id, int fd) {
    routing_data_t **bucket = GET_BUCKET(table, id);
    routing_data_t *data = malloc(sizeof(routing_data_t));
    if(data == NULL) {
        return UNABLE_TO_ALLOCATE_HEAP;
    }
    data->id = id;
    data->type = type;
    data->parent_id = current_id;
    data->next_hop_fd = fd;
    data->pid = pid;
    data->next = NULL;
    insert_routing_data_in_bucket(bucket, data);
    return OK;
}

error_code_t insert_direct_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t current_id, int fd) {
    return insert_direct_routing_data_pid(table, id, NO_PID, type, current_id, fd);
}

error_code_t insert_indirect_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t parent_id) {
    routing_data_t *parent = find_routing_data(table, parent_id);
    if(parent == NULL) {
        return ROUTE_NOT_FOUND;
    }
    routing_data_t **bucket = GET_BUCKET(table, id);
    routing_data_t *data = malloc(sizeof(routing_data_t));
    if(data == NULL) {
        return UNABLE_TO_ALLOCATE_HEAP;
    }
    data->id = id;
    data->type = type;
    data->parent_id = parent_id;
    data->next_hop_fd = parent->next_hop_fd;
    // `pid` is not set here because this function is never used by the Controller
    data->next = NULL;
    insert_routing_data_in_bucket(bucket, data);
    return OK;
}

routing_data_t* find_routing_data(routing_table_t table, device_id_t id) {
    routing_data_t *current = *GET_BUCKET(table, id);
    while(current != NULL && current->id < id) {
        current = current->next;
    }
    if(current != NULL && current->id == id) {
        return current;
    }
    return NULL;
}

routing_data_t* find_direct_routing_data(routing_table_t table, device_id_t parent_id, routing_data_t *last) {
    routing_data_t *current = last;
    if(current != NULL) {
        current = current->next;
    }
    id_hash_t b = current != NULL ? HASH_ID(current->id) : (last != NULL ? HASH_ID(last->id) + 1 : 0);
    for(; b < UNIQUE_ID_HASHES; b++) {
        if(current == NULL) {
            current = table[b];
        }
        while(current != NULL && current->parent_id != parent_id) {
            current = current->next;
        }
        if(current != NULL) {
            return current;
        }
    }
    return NULL;
}

routing_data_t* find_all_routing_data(routing_table_t table, device_id_t parent_id, routing_data_t *last) {
    if(last == NULL) { // Start search
        return find_direct_routing_data(table, parent_id, NULL);
    }
    // Resume search
    routing_data_t *child = find_direct_routing_data(table, last->id, NULL);
    if(child != NULL) { // Return child, move down
        return child;
    }
    routing_data_t *sibling = find_direct_routing_data(table, last->parent_id, last);
    if(sibling != NULL) { // Return sibling, move horizontally
        return sibling;
    }
    // Move up and horizontally, search for siblings of the parent, until the top is reached
    routing_data_t *parent = find_routing_data(table, last->parent_id);
    while(parent != NULL) {
        sibling = find_direct_routing_data(table, parent->parent_id, parent);
        if(sibling != NULL) {
            return sibling;
        }
        parent = find_routing_data(table, parent->parent_id);
    }
    return NULL;
}

void remove_routing_data(routing_table_t table, device_id_t id, device_id_t parent_id) {
    routing_data_t *data = find_routing_data(table, id);
    // Check if found and parent ID matches
    if(data == NULL || data->parent_id != parent_id) {
        return;
    }

    // Recursively remove all children
    routing_data_t *current = find_direct_routing_data(table, id, NULL);
    while(current != NULL) {
        /*
          In this case the parent ID will always match, but it's not important
          enough to write and use a different function without the check
         */
        remove_routing_data(table, current->id, current->parent_id);
        /*
          At this point `current` has been removed, so it's no longer a valid
          starting point for the search, the next result is the first
        */
        current = find_direct_routing_data(table, id, NULL);
    }

    // Remove the entry
    remove_routing_data_from_bucket(GET_BUCKET(table, id), id);
}

void insert_routing_data_in_bucket(routing_data_t **bucket, routing_data_t *data) {
    if(*bucket == NULL) { // Empty bucket
        *bucket = data;
    }
    else {
        routing_data_t *previous = NULL;
        routing_data_t *current = *bucket;
        while(current->next != NULL && current->id < data->id) {
            previous = current;
            current = current->next;
        }
        if(current->id == data->id) { // Replace previous data
            data->next = current->next;
            free(current);
        }
        else {
            data->next = current; // Insert
        }
        if(previous == NULL) {
            if(current->id < data->id) { // Put as second
                current->next = data;
                data->next = NULL;
            }
            else { // Put as first
                *bucket = data;
            }
        }
        else { // Put in the middle or at the end
            previous->next = data;
        }
    }
}

void remove_routing_data_from_bucket(routing_data_t **bucket, device_id_t id) {
    routing_data_t *current = *bucket;
    routing_data_t *previous = NULL;
    while(current != NULL && current->id < id) {
        previous = current;
        current = current->next;
    }
    if(current != NULL && current->id == id) {
        if(previous == NULL) { // Remove first
            *bucket = current->next;
        }
        else { // Remove other
            previous->next = current->next;
        }
        free(current);
    }
}

void print_routing_table(routing_table_t table) {
    for(id_hash_t b = 0; b < UNIQUE_ID_HASHES; b++) {
        routing_data_t *current = table[b];
        while(current != NULL) {
            print_routing_data(current);
            current = current->next;
        }
    }
}

void print_routing_data(routing_data_t *data) {
    if(data != NULL) {
        printf("Id: 0x%x, type: %1x, parent id: 0x%x, next hop fd: %d\n", data->id, data->type, data->parent_id, data->next_hop_fd);
    }
    else {
        printf("Not found\n");
    }
}