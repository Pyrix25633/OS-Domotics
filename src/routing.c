#define _XOPEN_SOURCE 700

#include "routing.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <utils.h>

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
    data->pid = pid;
    data->type = type;
    data->parent_id = current_id;
    data->next_hop_fd = fd;
    insert_routing_data_in_bucket(bucket, data);
    return OK;
}

error_code_t insert_direct_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t current_id, int fd) {
    return insert_direct_routing_data_pid(table, id, NO_PID, type, current_id, fd);
}

error_code_t insert_indirect_routing_data_pid(routing_table_t table, device_id_t id, pid_t pid, device_type_t type, device_id_t parent_id) {
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
    data->pid = pid;
    data->type = type;
    data->parent_id = parent_id;
    data->next_hop_fd = parent->next_hop_fd;
    // `pid` is not set here because this function is never used by the Controller
    insert_routing_data_in_bucket(bucket, data);
    return OK;
}

error_code_t insert_indirect_routing_data(routing_table_t table, device_id_t id, device_type_t type, device_id_t parent_id) {
    return insert_indirect_routing_data_pid(table, id, NO_PID, type, parent_id);
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
    while(parent != NULL && parent->id != parent_id) { // Stop at specified parent
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
        data->next = NULL;
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
            if(previous == NULL) { // First
                *bucket = data;
            }
            else { // Not first
                previous->next = data;
            }
            free(current);
            return;
        }
        // Insert new data
        if(current->id > data->id) { // Before
            if(previous == NULL) { // First
                *bucket = data;
            }
            else {
                previous->next = data;
            }
            data->next = current;
        }
        else { // After
            data->next = current->next;
            current->next = data;
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

error_code_t export_routing_table(routing_table_t table, device_id_t parent_id) {
    FILE *tmp_file = fopen(TMP_REGISTRY_FILE, "w"); // Create or truncate
    if(tmp_file == NULL) {
        return UNABLE_TO_OPEN_FILE;
    }

    char line[REGISTRY_LINE_SIZE];
    int length;
    error_code_t error_code = OK;

    routing_data_t *current = find_all_routing_data(table, parent_id, NULL);
    while(current != NULL) {
        length = snprintf(line, REGISTRY_LINE_SIZE, "%u %u\n", current->id, current->type);
        if(length >= REGISTRY_LINE_SIZE || length < 0
            || fputs(line, tmp_file) < 0) {
            error_code = UNABLE_TO_WRITE_FILE;
            break;
        }

        current = find_all_routing_data(table, parent_id, current);
    }

    if(fclose(tmp_file) < 0) {
        return UNABLE_TO_CLOSE_FILE;
    }
    if(IS_ERROR(error_code)) {
        return error_code;
    }
    if(rename(TMP_REGISTRY_FILE, REGISTRY_FILE) < 0) {
        // The rename is atomic and replaces the old registry file
        return UNABLE_TO_RENAME_FILE;
    }
    return OK;
}

error_code_t find_device_type(device_id_t id, device_type_t* type) {
    FILE *file = fopen(REGISTRY_FILE, "r");
    if(file == NULL) {
        if(errno == ENOENT) {
            return DEVICE_NOT_FOUND;
        }
        return UNABLE_TO_OPEN_FILE;
    }

    char line[REGISTRY_LINE_SIZE];
    char *token;
    char *last;
    device_id_t current_id;
    device_type_t current_type;
    int ret;
    error_code_t error_code = OK;
    bool found = false;
    int length;

    while(fgets(line, REGISTRY_LINE_SIZE, file) != NULL && !found) {
        length = strlen(line);
        if(length > 0 && line[length - 1] == '\n') {
            line[--length] = '\0'; // Remove new line
        }
        token = strtok_r(line, " ", &last);
        if(token == NULL) {
            error_code = REGISTRY_FORMAT_ERROR;
            break;
        }
        ret = string_to_unsigned(token);
        if(IS_RETURN_ERROR(ret)) {
            error_code = REGISTRY_FORMAT_ERROR;
            break;
        }
        current_id = ret;
        token = strtok_r(NULL, " ", &last);
        if(token == NULL) {
            error_code = REGISTRY_FORMAT_ERROR;
            break;
        }
        ret = string_to_unsigned(token);
        if(IS_RETURN_ERROR(ret)) {
            error_code = REGISTRY_FORMAT_ERROR;
            break;
        }
        current_type = ret;
        if(current_id == id) {
            *type = current_type;
            found = true;
        }
    }

    if(fclose(file) < 0) {
        return UNABLE_TO_CLOSE_FILE;
    }
    if(IS_ERROR(error_code)) {
        return error_code;
    }
    return found ? OK : DEVICE_NOT_FOUND;
}