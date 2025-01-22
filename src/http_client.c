/**
 * @file http_client.c
 * @brief HTTP client implementation
 * @author Rouge Lin
 * @date 2025-01-23
 */

#include "http_client.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

/*------------------------ HTTP communication module implementation ------------------------*/

size_t
curl_data_writer(char *buffer, size_t element_size,
                 size_t element_count, void *user_buffer)
{
    size_t data_size = element_size * element_count;
    http_response_t *response_buffer = (http_response_t *)user_buffer;

    char *new_buffer = realloc(response_buffer->payload, 
                              response_buffer->payload_size + data_size + 1);
    if (!new_buffer) return 0;

    response_buffer->payload = new_buffer;
    memcpy(&response_buffer->payload[response_buffer->payload_size], 
          buffer, data_size);
    response_buffer->payload_size += data_size;
    response_buffer->payload[response_buffer->payload_size] = '\0';
    return data_size;
}

CURLcode
perform_http_post (const char *url, const char *auth_header,
                   const char *payload, http_response_t *response)
{
    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) return CURLE_FAILED_INIT;

    struct curl_slist *header_list = NULL;
    header_list = curl_slist_append(header_list, "Content-Type: application/json");
    header_list = curl_slist_append(header_list, auth_header);

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_data_writer);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "deepseek-cli/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);

    CURLcode result = curl_easy_perform(curl_handle);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, 
                         &response->status_code);
    }
    
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl_handle);
    return result;
}


char *
construct_request_json (const api_config_t *config,
                        const chat_request_params_t *params,
                        int stream)
{
    cJSON *root_object = cJSON_CreateObject();
    if (!root_object) return NULL;

    if (!cJSON_AddStringToObject(root_object, "model", config->model_name)) goto error;

    cJSON *message_array = cJSON_AddArrayToObject(root_object, "messages");
    if (!message_array) goto error;

    cJSON *system_message = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(system_message, "role", "system") ||
        !cJSON_AddStringToObject(system_message, "content", 
             params->custom_prompt ? params->custom_prompt : config->system_prompt)) {
        cJSON_Delete(system_message);
        goto error;
    }
    cJSON_AddItemToArray(message_array, system_message);

    cJSON *user_message = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(user_message, "role", "user") ||
        !cJSON_AddStringToObject(user_message, "content", params->user_query)) {
        cJSON_Delete(user_message);
        goto error;
    }
    cJSON_AddItemToArray(message_array, user_message);

    if (stream) {
        if (!cJSON_AddTrueToObject(root_object, "stream")) goto error;
    } else {
        if (!cJSON_AddFalseToObject(root_object, "stream")) goto error;
    }

    char *json_output = cJSON_PrintUnformatted(root_object);
    cJSON_Delete(root_object);
    return json_output;

error:
    cJSON_Delete(root_object);
    return NULL;
}