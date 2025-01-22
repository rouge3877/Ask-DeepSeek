/**
 * @file utils.h
 * @brief Utility functions header
 * @note Contains helper functions and macros
 * @author Rouge Lin
 * @date 2025-01-23
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @def SAFE_FREE
 * @brief Free memory safely
 * @note Frees the memory and sets the pointer to NULL
 * @param ptr Pointer to the memory to free
 */
#define SAFE_FREE(ptr) do { free(ptr); (ptr) = NULL; } while(0)

/*------------------------ Utility functions implementation ------------------------*/

/**
 * @brief Output text to stdout in a streaming fashion
 * @param text_buffer Text buffer
 * @return void
 */
void stream_output (const char *text_buffer);

/**
 * @brief Trim leading and trailing whitespace
 * @param string_buffer String buffer
 * @return void
 */
void trim_whitespace (char *string_buffer);


#endif /* UTILS_H */