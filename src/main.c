#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

/* 配置常量 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef PRINT_DELAY_USEC
#define PRINT_DELAY_USEC 500  // 0.5ms/字符
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
    char* model;        // 新增model字段
    char* system_msg;   // 新增系统消息配置
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

/* 函数声明 - 请求处理模块 */
CurlResponse* send_chat_request(const ApiConfig* config, const ChatParams* params);
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
        /* 处理注释和空行 */
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';
        trim_whitespace(line);
        if (strlen(line) == 0) continue;

        /* 解析键值对 */
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

    for (int i = 0; i < 4; ++i) {
        if (search_paths[i] && access(search_paths[i], R_OK) == 0) {
            return search_paths[i];
        }
    }
    return NULL;
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
    
    // 系统消息
    cJSON* system_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(system_msg, "role", "system");
    cJSON_AddStringToObject(system_msg, "content", 
                          params->system_message ? params->system_message : config->system_msg);
    cJSON_AddItemToArray(messages, system_msg);

    // 用户消息
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
CurlResponse* send_chat_request(const ApiConfig* config, const ChatParams* params) {
    CurlResponse* response = calloc(1, sizeof(CurlResponse));
    if (!response) return NULL;

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    
    char* payload = build_request_json(config, params);
    if (!payload) {
        free(response);
        return NULL;
    }

    CURLcode res = http_post(config->base_url, auth_header, payload, response);
    free(payload);

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
    
    /* 解析消息内容 */
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        if (cJSON_IsString(content)) {
            response->content = strdup(content->valuestring);
        }
    }

    /* 解析token使用情况 */
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
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"<your question>\"\n", argv[0]);
        return EXIT_FAILURE;
    }

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

    /* 准备请求参数 */
    ChatParams params = {
        .user_message = argv[1],
        .system_message = NULL  // 使用配置中的默认系统消息
    };

    /* 发送并处理请求 */
    CurlResponse* curl_res = send_chat_request(config, &params);
    if (!curl_res) {
        config_free(config);
        return EXIT_FAILURE;
    }

    ChatResponse* response = handle_chat_response(curl_res);
    
    /* 处理响应 */
    if (response && response->content) {
        printf("Answer: ");
        print_slowly(response->content, PRINT_DELAY_USEC);
        printf("\nToken Usage:\n  Prompt: %d\n  Completion: %d\n  Total: %d\n",
               response->prompt_tokens, response->completion_tokens, response->total_tokens);
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