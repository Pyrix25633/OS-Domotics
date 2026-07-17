#define _XOPEN_SOURCE 700

#include "controller.h"

int main(int argc, char *argv[]) {
    routing_table_t table;
    device_id_t current_id = CONTROLLER_ID;

    init_routing_table(table);

    insert_direct_routing_data(table, 0x13, WINDOW_DEVICE, current_id, 5);
    insert_direct_routing_data(table, 0x15, WINDOW_DEVICE, current_id, 9);
    insert_indirect_routing_data(table, 0x18, WINDOW_DEVICE, 0x13);

    print_routing_table(table);

    remove_routing_data(table, 0x18);

    print_routing_table(table);

    //routing_data_t *data = find_routing_data(table, 0x13);
    //print_routing_data(data);

    return OK;
}