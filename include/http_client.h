/**
 * @file http_client.h
 * @brief HTTP communication module
 * @note Handles API request construction and execution
 * @author Rouge Lin
 * @date 2025-01-23
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "config.h"
#include <curl/curl.h>

/**
 * @struct http_response_t
 * @brief HTTP response data container
 * @var payload Response body data
 * @var payload_size Response body size
 * @var status_code HTTP status code
 */
typedef struct {
    char *payload;       /**< Response body data */
    size_t payload_size; /**< Response body size */
    long status_code;    /**< HTTP status code */
} http_response_t;

/**
 * @struct chat_request_params_t
 * @brief Structure for chat request parameters
 * @var user_query User input query content
 * @var custom_prompt Custom system prompt (optional)
 */
typedef struct {
    char *user_query;    /**< User input query content */
    char *custom_prompt; /**< Custom system prompt (optional) */
} chat_request_params_t;

/**
 * @brief Write data to buffer
 * @param buffer Data buffer
 * @param element_size Size of each data element
 * @param element_count Number of data elements
 * @param user_buffer User data buffer
 * @return Number of bytes written
 * @note Callback function for writing data in the CURL library
 */
size_t curl_data_writer(char *buffer, size_t element_size,
                        size_t element_count, void *user_buffer);

/**
 * @brief Perform an HTTP POST request
 * @param url Request URL
 * @param auth_header Authorization header
 * @param payload Request body data
 * @param response HTTP response data container
 * @return CURLcode type error code
 * @note Executes an HTTP POST request and writes the response data to the response
 * @note Uses the CURL library to perform the HTTP request
 */
CURLcode perform_http_post(const char *url, const char *auth_header,
                          const char *payload, http_response_t *response);

/**
 * @brief Build the JSON payload for requests
 * @param config Pointer to the API configuration structure
 * @param params Pointer to the chat request parameters structure
 * @param stream Whether to enable streaming
 * @return JSON formatted request body string
 * @note Constructs the JSON formatted request body for chat requests
 * @note The request body includes the model name, user input, system prompt, and streaming flag
 * @note The request body format is as follows:
 *     {
 *        "model": "model name",
 *       "messages": [
 *          {"role": "system", "content": "system prompt"},
 *         {"role": "user", "content": "user input"}
 *      ],
 *     "stream": true|false
 *    }
 * @note The streaming flag in the request body controls the API response mode
 */
char * construct_request_json (const api_config_t *config,
                        const chat_request_params_t *params,
                        int stream);

#endif