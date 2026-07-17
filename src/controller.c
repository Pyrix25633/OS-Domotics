#define _XOPEN_SOURCE 700

#include "controller.h"

int main(int argc, char *argv[]) {
    routing_table_t table;
    device_id_t current_id = CONTROLLER_ID;

    init_routing_table(table);

    insert_direct_routing_data(table, 0x13, WINDOW_DEVICE, current_id, 5);
    insert_direct_routing_data(table, 0x15, WINDOW_DEVICE, current_id, 9);
    insert_indirect_routing_data(table, 0x18, WINDOW_DEVICE, 0x13);
    insert_indirect_routing_data(table, 0x19, WINDOW_DEVICE, 0x15);
    insert_indirect_routing_data(table, 0x28, WINDOW_DEVICE, 0x13);
    insert_indirect_routing_data(table, 0x95, WINDOW_DEVICE, 0x13);
    insert_indirect_routing_data(table, 0x36, WINDOW_DEVICE, 0x15);

    //print_routing_table(table);

    routing_data_t *child = find_direct_routing_data(table, 0x15, NULL);

    while(child != NULL) {
        print_routing_data(child);
        child = find_direct_routing_data(table, 0x15, child);
    }

    //routing_data_t *data = find_routing_data(table, 0x13);
    //print_routing_data(data);

    return OK;
}