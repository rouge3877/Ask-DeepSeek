/**
 * @file api_handler.h
 * @brief API request handling module header
 * @note Manages chat API interactions
 * @author Rouge Lin
 * @date 2025-01-23
 */

#ifndef API_HANDLER_H
#define API_HANDLER_H

#include "http_client.h"
#include "config.h"

/**
 * @struct chat_response_t
 * @brief Structure for parsed chat response
 * @var content Generated response content
 * @var input_token_count Input token count
 * @var output_token_count Output token count
 * @var total_token_count Total token count
 */
typedef struct {
    char *content;           /**< Generated response content */
    int input_token_count;   /**< Input token count */
    int output_token_count;  /**< Output token count */
    int total_token_count;   /**< Total token count */
} chat_response_t;

/**
 * @brief Execute a non-streaming chat request
 * @param config Pointer to the API configuration structure
 * @param request_json JSON formatted request body string
 * @return Pointer to the HTTP response data container
 */
http_response_t * execute_chat_request (const api_config_t *config, const char *request_json);

/**
 * @brief Parse and return the chat response
 * @param http_res Pointer to the HTTP response data container
 * @return Pointer to the parsed chat response structure
 */
chat_response_t * parse_chat_response (const http_response_t *http_res);

#endif /* API_HANDLER_H */