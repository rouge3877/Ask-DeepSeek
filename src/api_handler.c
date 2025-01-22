/**
 * @file api_handler.c
 * @brief API request handler implementation
 * @note Processes API responses and error handling
 * @author Rouge Lin
 * @date 2025-01-23
 */

#include "utils.h"
#include "api_handler.h"
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

/*------------------------ API request handling module implementation ------------------------*/

http_response_t *
execute_chat_request (const api_config_t *config, const char *request_json)
{
    http_response_t *response = calloc(1, sizeof(http_response_t));
    if (!response) {
        perror("Memory allocation failed");
        return NULL;
    }

    char auth_header[256];
    int header_length = snprintf(auth_header, sizeof(auth_header),
                                "Authorization: Bearer %s", config->api_key);
    if (header_length >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Authorization header truncated\n");
        SAFE_FREE(response);
        return NULL;
    }

    CURLcode curl_status = perform_http_post(config->base_url, auth_header, request_json, response);

    if (curl_status != CURLE_OK) {
        fprintf(stderr, "HTTP request failed: %s\n", curl_easy_strerror(curl_status));
        SAFE_FREE(response->payload);
        SAFE_FREE(response);
        return NULL;
    }

    if (response->status_code != 200) {
        fprintf(stderr, "HTTP error %ld: %s\n", 
               response->status_code, 
               response->payload ? response->payload : "No response content");
        SAFE_FREE(response->payload);
        SAFE_FREE(response);
        return NULL;
    }

    return response;
}

chat_response_t *
parse_chat_response (const http_response_t *http_res)
{
    if (!http_res || !http_res->payload) {
        fprintf(stderr, "Received empty response\n");
        return NULL;
    }

    cJSON *root_object = cJSON_Parse(http_res->payload);
    if (!root_object) {
        fprintf(stderr, "JSON parsing failed\n");
        return NULL;
    }

    cJSON *error_object = cJSON_GetObjectItem(root_object, "error");
    if (error_object) {
        cJSON *error_message = cJSON_GetObjectItem(error_object, "message");
        fprintf(stderr, "API error: %s\n", 
               cJSON_IsString(error_message) ? error_message->valuestring : "Unknown error");
        cJSON_Delete(root_object);
        return NULL;
    }

    chat_response_t *parsed_response = calloc(1, sizeof(chat_response_t));
    if (!parsed_response) {
        perror("Memory allocation failed");
        cJSON_Delete(root_object);
        return NULL;
    }

    cJSON *choices_array = cJSON_GetObjectItem(root_object, "choices");
    if (!cJSON_IsArray(choices_array) || cJSON_GetArraySize(choices_array) == 0) {
        fprintf(stderr, "Invalid choices array\n");
        goto error;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices_array, 0);
    cJSON *message_object = cJSON_GetObjectItem(first_choice, "message");
    cJSON *content_object = cJSON_GetObjectItem(message_object, "content");
    if (!cJSON_IsString(content_object)) {
        fprintf(stderr, "Invalid content format\n");
        goto error;
    }

    parsed_response->content = strdup(content_object->valuestring);
    if (!parsed_response->content) {
        perror("String duplication failed");
        goto error;
    }

    cJSON *usage_object = cJSON_GetObjectItem(root_object, "usage");
    if (usage_object) {
        cJSON *input_tokens = cJSON_GetObjectItem(usage_object, "prompt_tokens");
        cJSON *output_tokens = cJSON_GetObjectItem(usage_object, "completion_tokens");
        cJSON *total_tokens = cJSON_GetObjectItem(usage_object, "total_tokens");
        parsed_response->input_token_count = input_tokens ? input_tokens->valueint : 0;
        parsed_response->output_token_count = output_tokens ? output_tokens->valueint : 0;
        parsed_response->total_token_count = total_tokens ? total_tokens->valueint : 0;
    }

    cJSON_Delete(root_object);
    return parsed_response;

error:
    cJSON_Delete(root_object);
    SAFE_FREE(parsed_response);
    return NULL;
}
