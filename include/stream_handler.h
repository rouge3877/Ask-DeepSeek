/**
 * @file stream_handler.h
 * @brief Streaming response handler header
 * @note Manages real-time response processing
 * @author Rouge Lin
 * @date 2025-01-23
 */

#ifndef STREAM_HANDLER_H
#define STREAM_HANDLER_H

#include "config.h"

/**
 * @struct stream_context_t
 * @brief Context for handling streaming output
 * @var buffer Data buffer
 * @var buffer_len Current buffer length
 * @var show_tokens Whether to show token statistics
 */
typedef struct {
    char buffer[4096];   /**< Data buffer */
    size_t buffer_len;   /**< Current buffer length */
    int show_tokens;     /**< Whether to show token statistics */
} stream_context_t;

/**
 * @brief Callback to handle streaming response
 * @param ptr Data buffer pointer
 * @param size Size of each data element
 * @param nmemb Number of data elements
 * @param userdata User data pointer
 * @return Number of bytes processed
 * @note Callback function for handling streaming response data
 */
size_t stream_data_callback (char *ptr, size_t size, size_t nmemb, void *userdata);

/**
 * @brief Process streamed data chunks
 * @param ctx Pointer to the streaming context
 * @return void
 */
void process_stream_data (stream_context_t *ctx);

/**
 * @brief Execute a streaming chat request
 * @param config Pointer to the API configuration structure
 * @param request_json JSON formatted request body string
 * @param show_tokens Whether to show token statistics
 * @return 0 on success, -1 on failure
 */
int execute_streaming_request (const api_config_t *config, const char *request_json,
                           int show_tokens);

#endif /* STREAM_HANDLER_H */