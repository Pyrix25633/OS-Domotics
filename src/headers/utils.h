/**
 * This file contains various utility function declarations
 */

#ifndef DOMOTICS_UTILS_H
#define DOMOTICS_UTILS_H

#include <sys/types.h>

/**
 * Measures the length of a possibly not NULL-terminated string
 * @param string The string to measure
 * @param max_length The maximum length of the string
 * @returns The length of the string, max_length if the string is not NULL-terminated
 */
size_t string_length(char *string, size_t max_length);

/**
 * Converts a string into an unsigned, checking for errors
 * @param string NULL-terminated string to be converted
 * @param 
 * @returns The converted number (positive),
 * `-CODE_FORMAT_ERROR` if the number is not correctly formatted
 */
int string_to_unsigned(char *string);

#endif
