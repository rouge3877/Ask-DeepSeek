/**
 * @file stream_handler.c
 * @brief Streaming response implementation
 * @note Handles chunked data processing
 * @author Rouge Lin
 * @date 2025-01-23
 */

#include "stream_handler.h"
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

/*------------------------ Streaming module implementation ------------------------*/

size_t
stream_data_callback (char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t data_size = size * nmemb;
    stream_context_t *ctx = (stream_context_t *)userdata;

    if (ctx->buffer_len + data_size >= sizeof(ctx->buffer)) {
        fprintf(stderr, "Stream buffer overflow\n");
        return 0;
    }

    memcpy(ctx->buffer + ctx->buffer_len, ptr, data_size);
    ctx->buffer_len += data_size;
    ctx->buffer[ctx->buffer_len] = '\0';

    process_stream_data(ctx);
    return data_size;
}

void
process_stream_data (stream_context_t *ctx)
{
    char *line_start = ctx->buffer;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';

        if (strncmp(line_start, "data: ", 6) == 0) {
            line_start += 6;
        }

        cJSON *root = cJSON_Parse(line_start);
        if (root) {
            cJSON *choices = cJSON_GetObjectItem(root, "choices");
            if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                if (delta) {
                    cJSON *content = cJSON_GetObjectItem(delta, "content");
                    if (cJSON_IsString(content)) {
                        printf("%s", content->valuestring);
                        fflush(stdout);
                    }
                }
            }
            cJSON_Delete(root);
        }

        line_start = line_end + 1;
    }

    size_t remaining = ctx->buffer_len - (line_start - ctx->buffer);
    memmove(ctx->buffer, line_start, remaining);
    ctx->buffer_len = remaining;
    ctx->buffer[remaining] = '\0';
}

int
execute_streaming_request (const api_config_t *config, const char *request_json,
                           int show_tokens)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    stream_context_t ctx = { .buffer_len = 0, .show_tokens = show_tokens };

    curl_easy_setopt(curl, CURLOPT_URL, config->base_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_data_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Request failed: %s\n", curl_easy_strerror(res));
    }

    /* Token usage unavailable in streaming mode */
    if (ctx.show_tokens) {
        fprintf(stderr, "\nToken usage unavailable in streaming mode\n");
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK ? 0 : -1;
}
