/**
 * @file utils.c
 * @brief Utility functions implementation
 * @note Provides common helper functions
 * @author Rouge Lin
 * @date 2025-01-23
 */

#include "utils.h"

/*------------------------ Utility functions implementation ------------------------*/

void
stream_output (const char *text_buffer)
{
    if (!text_buffer) return;

    for (size_t i = 0; text_buffer[i]; ++i) {
        putchar(text_buffer[i]);
        fflush(stdout);
    }
    putchar('\n');
}

void
trim_whitespace (char *string_buffer)
{
    if (!string_buffer) return;

    char *start_ptr = string_buffer;
    while (isspace((unsigned char)*start_ptr)) start_ptr++;
    memmove(string_buffer, start_ptr, strlen(start_ptr) + 1);

    char *end_ptr = string_buffer + strlen(string_buffer) - 1;
    while (end_ptr >= string_buffer && isspace((unsigned char)*end_ptr)) end_ptr--;
    *(end_ptr + 1) = '\0';
}
