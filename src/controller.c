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
    insert_indirect_routing_data(table, 0x96, WINDOW_DEVICE, 0x19);
    insert_indirect_routing_data(table, 0x39, WINDOW_DEVICE, 0x19);
    insert_indirect_routing_data(table, 0x47, WINDOW_DEVICE, 0x96);
    insert_indirect_routing_data(table, 0x11, WINDOW_DEVICE, 0x28);

    /* logical tree:
        0x13
            0x18
            0x28
                0x11
            0x95
        0x15
            0x19
                0x96
                    0x47
                0x39
            0x36

     */

    print_routing_table(table);

    /*routing_data_t *child = find_direct_routing_data(table, 0x19, NULL);
    while(child != NULL) {
        print_routing_data(child);
        child = find_direct_routing_data(table, 0x19, child);
    }*/

    remove_routing_data(table, 0x19, 0x15);

    printf("After removal\n");

    print_routing_table(table);

    //routing_data_t *data = find_routing_data(table, 0x13);
    //print_routing_data(data);

    return OK;
}