#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>

/* 配置常量 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef DEFAULT_MODEL
#define DEFAULT_MODEL "deepseek-chat"
#endif

#ifndef DEFAULT_SYSTEM_MSG
#define DEFAULT_SYSTEM_MSG "You are a helpful assistant."
#endif

/* 类型定义 */
typedef struct {
    char* api_key;
    char* base_url;
    char* model;
    char* system_msg;
} ApiConfig;

typedef struct {
    char* user_message;
    char* system_message;
} ChatParams;

typedef struct {
    char* content;
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
} ChatResponse;

typedef struct {
    char* memory;
    size_t size;
    long status_code;
} CurlResponse;

/* 函数声明 - 配置模块 */
ApiConfig* config_load(const char* config_path);
void config_free(ApiConfig* config);
const char* config_find_file();
void print_environment_json(const ApiConfig* config);

/* 函数声明 - 请求处理模块 */
CurlResponse* send_chat_request_with_payload(const ApiConfig* config, const char* payload);
ChatResponse* handle_chat_response(const CurlResponse* curl_res);

/* 函数声明 - HTTP模块 */
static CURLcode http_post(const char* url, const char* auth_header, 
                        const char* payload, CurlResponse* response);
static char* build_request_json(const ApiConfig* config, const ChatParams* params);

/* 函数声明 - 工具模块 */
void print_slowly(const char* text);  // 修改函数签名
void trim_whitespace(char* str);

//------------------------ 配置模块实现 ------------------------//
ApiConfig* config_load(const char* config_path) {
    FILE* fp = fopen(config_path, "r");
    if (!fp) return NULL;

    ApiConfig* config = calloc(1, sizeof(ApiConfig));
    if (!config) {
        fclose(fp);
        return NULL;
    }

    if (!(config->model = strdup(DEFAULT_MODEL)) ||
        !(config->system_msg = strdup(DEFAULT_SYSTEM_MSG))) {
        config_free(config);
        fclose(fp);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';
        trim_whitespace(line);
        if (strlen(line) == 0) continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = line;
        char* value = eq + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        char** target = NULL;
        if (strcmp(key, "API_KEY") == 0) target = &config->api_key;
        else if (strcmp(key, "BASE_URL") == 0) target = &config->base_url;
        else if (strcmp(key, "MODEL") == 0) target = &config->model;
        else if (strcmp(key, "SYSTEM_MSG") == 0) target = &config->system_msg;

        if (target) {
            char* new_value = strdup(value);
            if (!new_value) {
                config_free(config);
                fclose(fp);
                return NULL;
            }
            free(*target);
            *target = new_value;
        }
    }

    fclose(fp);
    return config;
}

void config_free(ApiConfig* config) {
    if (config) {
        free(config->api_key);
        free(config->base_url);
        free(config->model);
        free(config->system_msg);
        free(config);
    }
}

const char* config_find_file() {
    static const char* search_paths[] = {
        "./.adsenv",
        NULL,  // ~/.adsenv
        NULL,  // ~/.config/.adsenv
        "/etc/.adsenv"
    };

    const char* home = getenv("HOME");
    if (home) {
        static char path1[PATH_MAX], path2[PATH_MAX];
        snprintf(path1, PATH_MAX, "%s/.adsenv", home);
        snprintf(path2, PATH_MAX, "%s/.config/.adsenv", home);
        search_paths[1] = path1;
        search_paths[2] = path2;
    }

    for (size_t i = 0; i < sizeof(search_paths)/sizeof(search_paths[0]); ++i) {
        if (search_paths[i] && access(search_paths[i], R_OK) == 0) {
            return search_paths[i];
        }
    }
    return NULL;
}

void print_environment_json(const ApiConfig* config) {
    cJSON *root = cJSON_CreateObject();
    cJSON *config_obj = cJSON_AddObjectToObject(root, "config");
    
    cJSON_AddStringToObject(config_obj, "API_KEY", config->api_key ? config->api_key : "");
    cJSON_AddStringToObject(config_obj, "BASE_URL", config->base_url ? config->base_url : "");
    cJSON_AddStringToObject(config_obj, "MODEL", config->model);
    cJSON_AddStringToObject(config_obj, "SYSTEM_MSG", config->system_msg);
    
    cJSON *macros = cJSON_AddObjectToObject(root, "macros");
    cJSON_AddStringToObject(macros, "DEFAULT_MODEL", DEFAULT_MODEL);
    cJSON_AddStringToObject(macros, "DEFAULT_SYSTEM_MSG", DEFAULT_SYSTEM_MSG);
    cJSON_AddNumberToObject(macros, "PATH_MAX", PATH_MAX);
    
    char *json_str = cJSON_Print(root);
    if (json_str) {
        printf("%s\n", json_str);
        free(json_str);
    }
    cJSON_Delete(root);
}

//------------------------ HTTP模块实现 ------------------------//
static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    CurlResponse* mem = (CurlResponse*)userp;

    char* ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&mem->memory[mem->size], contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';
    return realsize;
}

static CURLcode http_post(const char* url, const char* auth_header,
                        const char* payload, CurlResponse* response) {
    CURL* curl = curl_easy_init();
    if (!curl) return CURLE_FAILED_INIT;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status_code);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

static char* build_request_json(const ApiConfig* config, const ChatParams* params) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", config->model);

    cJSON* messages = cJSON_AddArrayToObject(root, "messages");
    if (!messages) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON* system_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(system_msg, "role", "system");
    cJSON_AddStringToObject(system_msg, "content", 
                          params->system_message ? params->system_message : config->system_msg);
    cJSON_AddItemToArray(messages, system_msg);

    cJSON* user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", params->user_message);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddFalseToObject(root, "stream");
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

//------------------------ 请求处理模块 ------------------------//
CurlResponse* send_chat_request_with_payload(const ApiConfig* config, const char* payload) {
    CurlResponse* response = calloc(1, sizeof(CurlResponse));
    if (!response) return NULL;

    char auth_header[256];
    int needed = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    if (needed >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Authorization header truncated\n");
        free(response);
        return NULL;
    }

    CURLcode res = http_post(config->base_url, auth_header, payload, response);

    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP request failed: %s\n", curl_easy_strerror(res));
        free(response->memory);
        free(response);
        return NULL;
    }

    if (response->status_code != 200) {
        fprintf(stderr, "HTTP error %ld: %s\n", response->status_code, response->memory ? response->memory : "No response body");
        free(response->memory);
        free(response);
        return NULL;
    }

    return response;
}

ChatResponse* handle_chat_response(const CurlResponse* curl_res) {
    if (!curl_res || !curl_res->memory) return NULL;

    cJSON* root = cJSON_Parse(curl_res->memory);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON response\n");
        return NULL;
    }

    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON* msg = cJSON_GetObjectItem(error, "message");
        fprintf(stderr, "API Error: %s\n", cJSON_IsString(msg) ? msg->valuestring : "Unknown error");
        cJSON_Delete(root);
        return NULL;
    }

    ChatResponse* response = calloc(1, sizeof(ChatResponse));
    if (!response) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        fprintf(stderr, "Invalid choices array\n");
        goto error;
    }

    cJSON* first_choice = cJSON_GetArrayItem(choices, 0);
    cJSON* message = cJSON_GetObjectItem(first_choice, "message");
    cJSON* content = cJSON_GetObjectItem(message, "content");
    if (!cJSON_IsString(content)) {
        fprintf(stderr, "Invalid content format\n");
        goto error;
    }

    response->content = strdup(content->valuestring);
    if (!response->content) {
        fprintf(stderr, "Memory allocation failed\n");
        goto error;
    }

    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        response->prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens")->valueint;
        response->completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens")->valueint;
        response->total_tokens = cJSON_GetObjectItem(usage, "total_tokens")->valueint;
    }

    cJSON_Delete(root);
    return response;

error:
    cJSON_Delete(root);
    free(response);
    return NULL;
}

//------------------------ 工具模块实现 ------------------------//
void print_slowly(const char* text) {  // 直接输出完整文本
    printf("%s\n", text);
    fflush(stdout);
}

void trim_whitespace(char* str) {
    char* start = str;
    while (isspace((unsigned char)*start)) start++;
    memmove(str, start, strlen(start) + 1);

    char* end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

//------------------------ 主程序 ------------------------//
int main(int argc, char** argv) {
    int print_env_flag = 0;
    int count_token_flag = 0;
    int echo_flag = 0;
    int just_json_flag = 0;
    int opt;
    
    static struct option long_options[] = {
        {"print-env", no_argument, 0, 'p'},
        {"just-json", no_argument, 0, 'j'},
        {"count-token", no_argument, 0, 'c'},
        {"echo", no_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "pjc:e", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                print_env_flag = 1;
                break;
            case 'j':
                just_json_flag = 1;
                break;
            case 'c':
                count_token_flag = 1;
                break;
            case 'e':
                echo_flag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-p] [-j] [-c] [-e] \"<question>\"\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (print_env_flag) {
        const char* config_path = config_find_file();
        if (!config_path) {
            fprintf(stderr, "No config file found\n");
            return EXIT_FAILURE;
        }

        ApiConfig* config = config_load(config_path);
        if (!config) {
            fprintf(stderr, "Invalid config\n");
            return EXIT_FAILURE;
        }

        print_environment_json(config);
        config_free(config);
        return EXIT_SUCCESS;
    }

    if (optind >= argc) {
        fprintf(stderr, "Missing question\n");
        return EXIT_FAILURE;
    }
    char* user_question = argv[optind];

    const char* config_path = config_find_file();
    if (!config_path) {
        fprintf(stderr, "No config file found\n");
        return EXIT_FAILURE;
    }

    ApiConfig* config = config_load(config_path);
    if (!config || !config->api_key || !config->base_url) {
        fprintf(stderr, "Invalid configuration\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    if (echo_flag) {
        printf("\nInput: %s\n", user_question);
    }

    ChatParams params = {
        .user_message = user_question,
        .system_message = NULL
    };

    char* payload = build_request_json(config, &params);
    if (!payload) {
        fprintf(stderr, "Failed to create payload\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    if (just_json_flag) {
        printf("%s\n", payload);
        free(payload);
        config_free(config);
        return EXIT_SUCCESS;
    }

    CurlResponse* curl_res = send_chat_request_with_payload(config, payload);
    free(payload);
    if (!curl_res) {
        config_free(config);
        return EXIT_FAILURE;
    }

    ChatResponse* response = handle_chat_response(curl_res);
    if (response && response->content) {
        printf("\nAnswer: ");
        print_slowly(response->content);
        
        if (count_token_flag) {
            printf("\nToken Usage:\n  Prompt: %d\n  Completion: %d\n  Total: %d\n",
                response->prompt_tokens, response->completion_tokens, response->total_tokens);
        }
    } else {
        fprintf(stderr, "Invalid response\n");
    }

    free(curl_res->memory);
    free(curl_res);
    free(response->content);
    free(response);
    config_free(config);
    return EXIT_SUCCESS;
}