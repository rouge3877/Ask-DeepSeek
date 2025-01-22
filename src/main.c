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

#ifndef PRINT_DELAY_USEC
#define PRINT_DELAY_USEC 10000  // 10ms/字符
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
void print_slowly(const char* text, useconds_t delay);
void trim_whitespace(char* str);

//------------------------ 配置模块实现 ------------------------//
ApiConfig* config_load(const char* config_path) {
    FILE* fp = fopen(config_path, "r");
    if (!fp) return NULL;

    ApiConfig* config = calloc(1, sizeof(ApiConfig));
    char line[256];

    // 设置默认值
    config->model = strdup(DEFAULT_MODEL);
    config->system_msg = strdup(DEFAULT_SYSTEM_MSG);

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

        if (strcmp(key, "API_KEY") == 0) {
            config->api_key = strdup(value);
        } else if (strcmp(key, "BASE_URL") == 0) {
            config->base_url = strdup(value);
        } else if (strcmp(key, "MODEL") == 0) {
            free(config->model);
            config->model = strdup(value);
        } else if (strcmp(key, "SYSTEM_MSG") == 0) {
            free(config->system_msg);
            config->system_msg = strdup(value);
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

    size_t path_len = sizeof(search_paths) / sizeof(search_paths[0]);


    for (int i = 0; i < path_len; ++i) {
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
    cJSON_AddNumberToObject(macros, "PRINT_DELAY_USEC", PRINT_DELAY_USEC);
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

    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

static char* build_request_json(const ApiConfig* config, const ChatParams* params) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", config->model);

    cJSON* messages = cJSON_AddArrayToObject(root, "messages");
    
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
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);

    CURLcode res = http_post(config->base_url, auth_header, payload, response);

    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP request failed: %s\n", curl_easy_strerror(res));
        free(response->memory);
        free(response);
        return NULL;
    }
    return response;
}

ChatResponse* handle_chat_response(const CurlResponse* curl_res) {
    if (!curl_res || !curl_res->memory) return NULL;

    cJSON* root = cJSON_Parse(curl_res->memory);
    if (!root) return NULL;

    ChatResponse* response = calloc(1, sizeof(ChatResponse));
    
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        if (cJSON_IsString(content)) {
            response->content = strdup(content->valuestring);
        }
    }

    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        response->prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens")->valueint;
        response->completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens")->valueint;
        response->total_tokens = cJSON_GetObjectItem(usage, "total_tokens")->valueint;
    }

    cJSON_Delete(root);
    return response;
}

//------------------------ 工具模块实现 ------------------------//
void print_slowly(const char* text, useconds_t delay) {
    for (size_t i = 0; text[i]; ++i) {
        putchar(text[i]);
        fflush(stdout);
        usleep(delay);
    }
    putchar('\n');
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
                fprintf(stderr, "Usage: %s [-p] [-j] [-c] [-e] \"<your question>\"\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* 处理打印环境配置请求 */
    if (print_env_flag) {
        const char* config_path = config_find_file();
        if (!config_path) {
            fprintf(stderr, "Error: No valid config file found\n");
            return EXIT_FAILURE;
        }

        ApiConfig* config = config_load(config_path);
        if (!config) {
            fprintf(stderr, "Error: Invalid configuration\n");
            return EXIT_FAILURE;
        }

        print_environment_json(config);
        config_free(config);
        return EXIT_SUCCESS;
    }

    /* 检查剩余参数 */
    if (optind >= argc) {
        fprintf(stderr, "Error: Missing user question\n");
        fprintf(stderr, "Usage: %s [-p] [-j] [-c] [-e] \"<your question>\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    char* user_question = argv[optind];

    /* 初始化配置 */
    const char* config_path = config_find_file();
    if (!config_path) {
        fprintf(stderr, "Error: No valid config file found\n");
        return EXIT_FAILURE;
    }

    ApiConfig* config = config_load(config_path);
    if (!config || !config->api_key || !config->base_url) {
        fprintf(stderr, "Error: Invalid configuration\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    /* 处理输入回显 */
    if (echo_flag) {
        printf("\nInput: %s\n", user_question);
    }

    /* 准备请求参数 */
    ChatParams params = {
        .user_message = user_question,
        .system_message = NULL
    };

    /* 构建请求JSON */
    char* payload = build_request_json(config, &params);
    if (!payload) {
        fprintf(stderr, "Error: Failed to build request JSON\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    /* 处理仅打印JSON请求的情况 */
    if (just_json_flag) {
        printf("%s\n", payload);
        free(payload);
        config_free(config);
        return EXIT_SUCCESS;
    }

    /* 发送并处理请求 */
    CurlResponse* curl_res = send_chat_request_with_payload(config, payload);
    free(payload);

    if (!curl_res) {
        config_free(config);
        return EXIT_FAILURE;
    }

    ChatResponse* response = handle_chat_response(curl_res);
    
    /* 处理响应 */
    if (response && response->content) {
        printf("\nAnswer: ");
        print_slowly(response->content, PRINT_DELAY_USEC);
        
        if (count_token_flag) {
            printf("\nToken Usage:\n  Prompt: %d\n  Completion: %d\n  Total: %d\n",
                response->prompt_tokens, response->completion_tokens, response->total_tokens);
        }
    } else {
        fprintf(stderr, "Error: Invalid API response\n");
    }

    /* 清理资源 */
    free(curl_res->memory);
    free(curl_res);
    free(response->content);
    free(response);
    config_free(config);
    return EXIT_SUCCESS;
}