#include "utils.h"
#include "return_codes.h"

size_t string_length(char *string, size_t max_length) {
    unsigned i;
    for(i = 0; i < max_length; i++) {
        if(string[i] == '\0') {
            return i;
        }
    }
    return i;
}

int string_to_unsigned(char *string) {
    unsigned code = 0;
    for(unsigned i = 0; string[i] != '\0'; i++) {
        char digit = string[i];
        if(digit < '0' || digit > '9') {
            return -CODE_FORMAT_ERROR;
        }
        code *= 10;
        code += digit - '0';
    }
    return code;
}