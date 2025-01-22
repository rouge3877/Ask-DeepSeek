#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* 第三方库头文件 */
#include <curl/curl.h>
#include <cjson/cJSON.h>

/* 配置常量 */
#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifndef DEFAULT_MODEL
# define DEFAULT_MODEL "deepseek-chat"
#endif

#ifndef DEFAULT_SYSTEM_MSG
# define DEFAULT_SYSTEM_MSG "You are a helpful assistant."
#endif

/* 类型定义 */
typedef struct {
    char *api_key;
    char *base_url;
    char *model;
    char *system_msg;
} api_config;

typedef struct {
    char *user_message;
    char *system_message;
} chat_params;

typedef struct {
    char *content;
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
} chat_response;

typedef struct {
    char *memory;
    size_t size;
    long status_code;
} curl_response;

/* 函数声明 - 配置模块 */
api_config *config_load(const char *config_path);
void config_free(api_config *config);
const char *config_find_file(void);
void print_environment_json(const api_config *config);

/* 函数声明 - 请求处理模块 */
curl_response *send_chat_request(const api_config *config, const char *payload);
chat_response *parse_chat_response(const curl_response *curl_res);

/* 函数声明 - HTTP模块 */
static CURLcode http_post(const char *url, const char *auth_header,
                          const char *payload, curl_response *response);
static char *build_request_json(const api_config *config, const chat_params *params);

/* 函数声明 - 工具模块 */
void stream_print(const char *text);
void trim_whitespace(char *str);

/* 辅助宏 */
#define SAFE_FREE(ptr) do { free(ptr); (ptr) = NULL; } while(0)

/*------------------------ 配置模块实现 ------------------------*/
api_config *config_load(const char *config_path)
{
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        perror("fopen config failed");
        return NULL;
    }

    api_config *config = calloc(1, sizeof(api_config));
    if (!config) {
        fclose(fp);
        return NULL;
    }

    /* 初始化默认值 */
    if (!(config->model = strdup(DEFAULT_MODEL)) ||
        !(config->system_msg = strdup(DEFAULT_SYSTEM_MSG))) {
        perror("strdup failed");
        config_free(config);
        fclose(fp);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* 处理注释和空行 */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        trim_whitespace(line);
        if (line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;  /* 忽略无效行 */
        *eq = '\0';

        char *key = line;
        char *value = eq + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        /* 动态更新配置项 */
        char **target = NULL;
        if (strcmp(key, "API_KEY") == 0) {
            target = &config->api_key;
        } else if (strcmp(key, "BASE_URL") == 0) {
            target = &config->base_url;
        } else if (strcmp(key, "MODEL") == 0) {
            target = &config->model;
        } else if (strcmp(key, "SYSTEM_MSG") == 0) {
            target = &config->system_msg;
        }

        if (target) {
            char *new_value = strdup(value);
            if (!new_value) {
                perror("strdup failed");
                config_free(config);
                fclose(fp);
                return NULL;
            }
            free(*target);  /* 释放旧值 */
            *target = new_value;
        }
    }

    if (ferror(fp)) {
        perror("fgets error");
        config_free(config);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    return config;
}

void config_free(api_config *config)
{
    if (config) {
        SAFE_FREE(config->api_key);
        SAFE_FREE(config->base_url);
        SAFE_FREE(config->model);
        SAFE_FREE(config->system_msg);
        SAFE_FREE(config);
    }
}

const char *config_find_file(void)
{
    static const char *search_paths[] = {
        "./.adsenv",
        NULL,  /* ~/.adsenv */
        NULL,  /* ~/.config/.adsenv */
        "/etc/.adsenv"
    };

    const char *home = getenv("HOME");
    if (home) {
        static char path1[PATH_MAX], path2[PATH_MAX];
        snprintf(path1, sizeof(path1), "%s/.adsenv", home);
        snprintf(path2, sizeof(path2), "%s/.config/.adsenv", home);
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

void print_environment_json(const api_config *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *config_obj = cJSON_AddObjectToObject(root, "config");
    
    cJSON_AddStringToObject(config_obj, "API_KEY", 
                           config->api_key ? config->api_key : "");
    cJSON_AddStringToObject(config_obj, "BASE_URL", 
                           config->base_url ? config->base_url : "");
    cJSON_AddStringToObject(config_obj, "MODEL", config->model);
    cJSON_AddStringToObject(config_obj, "SYSTEM_MSG", config->system_msg);
    
    cJSON *macros = cJSON_AddObjectToObject(root, "macros");
    cJSON_AddStringToObject(macros, "DEFAULT_MODEL", DEFAULT_MODEL);
    cJSON_AddStringToObject(macros, "DEFAULT_SYSTEM_MSG", DEFAULT_SYSTEM_MSG);
    cJSON_AddNumberToObject(macros, "PATH_MAX", PATH_MAX);
    
    char *json_str = cJSON_Print(root);
    if (json_str) {
        printf("%s\n", json_str);
        SAFE_FREE(json_str);
    }
    cJSON_Delete(root);
}

/*------------------------ HTTP模块实现 ------------------------*/
static size_t curl_write_cb(void *contents, size_t size, 
                           size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    curl_response *mem = (curl_response *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&mem->memory[mem->size], contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';
    return realsize;
}

static CURLcode http_post(const char *url, const char *auth_header,
                         const char *payload, curl_response *response)
{
    CURL *curl = curl_easy_init();
    if (!curl) return CURLE_FAILED_INIT;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);  /* 设置30秒超时 */

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status_code);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

static char *build_request_json(const api_config *config, 
                               const chat_params *params)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    if (!cJSON_AddStringToObject(root, "model", config->model)) goto error;

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    if (!messages) goto error;

    /* 系统消息 */
    cJSON *sys_msg = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(sys_msg, "role", "system") ||
        !cJSON_AddStringToObject(sys_msg, "content", 
             params->system_message ? params->system_message : config->system_msg)) {
        cJSON_Delete(sys_msg);
        goto error;
    }
    cJSON_AddItemToArray(messages, sys_msg);

    /* 用户消息 */
    cJSON *user_msg = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(user_msg, "role", "user") ||
        !cJSON_AddStringToObject(user_msg, "content", params->user_message)) {
        cJSON_Delete(user_msg);
        goto error;
    }
    cJSON_AddItemToArray(messages, user_msg);

    /* 禁用流式输出 */
    if (!cJSON_AddFalseToObject(root, "stream")) goto error;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;

error:
    cJSON_Delete(root);
    return NULL;
}

/*------------------------ 请求处理模块 ------------------------*/
curl_response *send_chat_request(const api_config *config, const char *payload)
{
    curl_response *response = calloc(1, sizeof(curl_response));
    if (!response) {
        perror("calloc failed");
        return NULL;
    }

    char auth_header[256];
    int needed = snprintf(auth_header, sizeof(auth_header),
                         "Authorization: Bearer %s", config->api_key);
    if (needed >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Authorization header truncated\n");
        SAFE_FREE(response);
        return NULL;
    }

    CURLcode res = http_post(config->base_url, auth_header, payload, response);

    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP request failed: %s\n", curl_easy_strerror(res));
        SAFE_FREE(response->memory);
        SAFE_FREE(response);
        return NULL;
    }

    if (response->status_code != 200) {
        fprintf(stderr, "HTTP error %ld: %s\n", 
               response->status_code, 
               response->memory ? response->memory : "No response body");
        SAFE_FREE(response->memory);
        SAFE_FREE(response);
        return NULL;
    }

    return response;
}

chat_response *parse_chat_response(const curl_response *curl_res)
{
    if (!curl_res || !curl_res->memory) {
        fprintf(stderr, "Empty response\n");
        return NULL;
    }

    cJSON *root = cJSON_Parse(curl_res->memory);
    if (!root) {
        fprintf(stderr, "JSON parse failed\n");
        return NULL;
    }

    /* 处理API错误 */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        fprintf(stderr, "API Error: %s\n", 
               cJSON_IsString(msg) ? msg->valuestring : "Unknown error");
        cJSON_Delete(root);
        return NULL;
    }

    chat_response *response = calloc(1, sizeof(chat_response));
    if (!response) {
        perror("calloc failed");
        cJSON_Delete(root);
        return NULL;
    }

    /* 解析内容 */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        fprintf(stderr, "Invalid choices array\n");
        goto error;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!cJSON_IsString(content)) {
        fprintf(stderr, "Invalid content format\n");
        goto error;
    }

    response->content = strdup(content->valuestring);
    if (!response->content) {
        perror("strdup failed");
        goto error;
    }

    /* 解析token用量 */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *prompt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *completion = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON *total = cJSON_GetObjectItem(usage, "total_tokens");
        response->prompt_tokens = prompt ? prompt->valueint : 0;
        response->completion_tokens = completion ? completion->valueint : 0;
        response->total_tokens = total ? total->valueint : 0;
    }

    cJSON_Delete(root);
    return response;

error:
    cJSON_Delete(root);
    SAFE_FREE(response);
    return NULL;
}

/*------------------------ 工具模块实现 ------------------------*/
void stream_print(const char *text)
{
    if (!text) return;

    for (size_t i = 0; text[i]; ++i) {
        putchar(text[i]);
        fflush(stdout);
    }
    putchar('\n');
}

void trim_whitespace(char *str)
{
    if (!str) return;

    /* 去除前导空格 */
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    memmove(str, start, strlen(start) + 1);

    /* 去除尾部空格 */
    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

/*------------------------ 帮助信息 ------------------------*/
static void usage(const char *progname, FILE *stream, int exit_code)
{
    fprintf(stream, "Usage: %s [OPTION]... \"<QUESTION>\"\n", progname);
    fprintf(stream, "Command-line interface for DeepSeek LLM API\n\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  -p, --print-env                 Print current configuration and exit\n");
    fprintf(stream, "  -j, --just-json                 Generate request JSON without sending to API\n");
    fprintf(stream, "  -c, --count-token               Show token usage statistics\n");
    fprintf(stream, "  -e, --echo                      Echo the user's input question\n");
    fprintf(stream, "  -h, --help                      Display this help and exit\n");
    fprintf(stream, "\nExamples:\n");
    fprintf(stream, "  %s -p                        # Show current config\n", progname);
    fprintf(stream, "  %s -j -e \"Your question\"    # Generate JSON and echo input\n", progname);
    exit(exit_code);
}

/*------------------------ 参数解析 ------------------------*/
static int parse_arguments(int argc, char **argv,
                          int *print_env, int *count_token, int *echo, 
                          int *just_json, char **question)
{
    static struct option long_options[] = {
        {"print-env",     no_argument,       NULL, 'p'},
        {"just-json",     no_argument,       NULL, 'j'},
        {"count-token",   no_argument,       NULL, 'c'},
        {"echo",          no_argument,       NULL, 'e'},
        {"help",          no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "pjc:eh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'p':
            *print_env = 1;
            break;
        case 'j':
            *just_json = 1;
            break;
        case 'c':
            *count_token = 1;
            break;
        case 'e':
            *echo = 1;
            break;
        case 'h':
            usage(argv[0], stdout, EXIT_SUCCESS);
            break;
        case '?':
            usage(argv[0], stderr, EXIT_FAILURE);
            break;
        default:
            usage(argv[0], stderr, EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing required QUESTION argument\n", argv[0]);
        usage(argv[0], stderr, EXIT_FAILURE);
    }
    *question = argv[optind];
    return 0;
}

/*------------------------ 主程序 ------------------------*/
int main(int argc, char **argv)
{
    srand(time(NULL));

    /* 解析命令行参数 */
    int print_env = 0, count_token = 0, echo = 0, just_json = 0;
    char *user_question = NULL;

    if (parse_arguments(argc, argv, &print_env, 
                       &count_token, &echo, &just_json, &user_question) != 0) {
        return EXIT_FAILURE;
    }

    if (print_env) {
        const char *config_path = config_find_file();
        if (!config_path) {
            fprintf(stderr, "No configuration file found\n");
            return EXIT_FAILURE;
        }

        api_config *config = config_load(config_path);
        if (!config) {
            fprintf(stderr, "Failed to load configuration\n");
            return EXIT_FAILURE;
        }

        print_environment_json(config);
        config_free(config);
        return EXIT_SUCCESS;
    }

    /* 加载配置文件 */
    const char *config_path = config_find_file();
    if (!config_path) {
        fprintf(stderr, "Configuration file not found\n");
        return EXIT_FAILURE;
    }

    api_config *config = config_load(config_path);
    if (!config || !config->api_key || !config->base_url) {
        fprintf(stderr, "Invalid configuration parameters\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    if (echo) {
        printf("\nInput: %s\n", user_question);
    }

    /* 构建请求参数 */
    chat_params params = {
        .user_message = user_question,
        .system_message = NULL
    };

    char *payload = build_request_json(config, &params);
    if (!payload) {
        fprintf(stderr, "Failed to construct request payload\n");
        config_free(config);
        return EXIT_FAILURE;
    }

    if (just_json) {
        printf("%s\n", payload);
        SAFE_FREE(payload);
        config_free(config);
        return EXIT_SUCCESS;
    }

    /* 发送请求并处理响应 */
    curl_response *curl_res = send_chat_request(config, payload);
    SAFE_FREE(payload);
    if (!curl_res) {
        config_free(config);
        return EXIT_FAILURE;
    }

    chat_response *response = parse_chat_response(curl_res);
    if (response && response->content) {
        printf("\nAnswer: ");
        stream_print(response->content);
        
        if (count_token) {
            printf("\nToken Usage:\n  Prompt: %d\n  Completion: %d\n  Total: %d\n",
                  response->prompt_tokens, response->completion_tokens, 
                  response->total_tokens);
        }
    } else {
        fprintf(stderr, "Failed to get valid response\n");
    }

    /* 清理资源 */
    SAFE_FREE(curl_res->memory);
    SAFE_FREE(curl_res);
    if (response) {
        SAFE_FREE(response->content);
        SAFE_FREE(response);
    }
    config_free(config);

    return EXIT_SUCCESS;
}