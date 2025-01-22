/* 
 * Ask-DeepSeek - DeepSeek 大模型的命令行接口工具
 * 基于 libcurl 和 cJSON 库实现 API 通信
 * 遵循 MIT 许可证发行
 */

/* 系统头文件 */
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
# define PATH_MAX 4096                /* 最大文件路径长度 */
#endif

#ifndef DEFAULT_MODEL
# define DEFAULT_MODEL "deepseek-chat" /* 默认模型名称 */
#endif

#ifndef DEFAULT_SYSTEM_PROMPT
# define DEFAULT_SYSTEM_PROMPT "You are a helpful assistant." /* 默认系统提示词 */
#endif

/* 类型定义 */

/**
 * @struct api_config_t
 * @brief 存储API配置参数的结构体
 */
typedef struct {
    char *api_key;       /**< API访问密钥 */
    char *base_url;      /**< API端点基础URL */
    char *model_name;    /**< 使用的模型名称 */
    char *system_prompt; /**< 系统级提示信息 */
} api_config_t;

/**
 * @struct chat_request_params_t
 * @brief 聊天请求参数结构体
 */
typedef struct {
    char *user_query;    /**< 用户输入的查询内容 */
    char *custom_prompt; /**< 自定义系统提示（可选） */
} chat_request_params_t;

/**
 * @struct chat_response_t
 * @brief 解析后的聊天响应结构体
 */
typedef struct {
    char *content;           /**< 生成的响应内容 */
    int input_token_count;   /**< 输入令牌计数 */
    int output_token_count;  /**< 输出令牌计数 */
    int total_token_count;   /**< 总令牌计数 */
} chat_response_t;

/**
 * @struct http_response_t
 * @brief HTTP响应数据容器
 */
typedef struct {
    char *payload;       /**< 响应体数据 */
    size_t payload_size; /**< 响应体大小 */
    long status_code;    /**< HTTP状态码 */
} http_response_t;

/*------------------------ 函数声明 ------------------------*/

/* 配置管理模块 */
api_config_t *load_configuration(const char *config_path);
void free_configuration(api_config_t *config);
const char *locate_config_file(void);
void dump_configuration_json(const api_config_t *config);

/* API请求处理模块 */
http_response_t *execute_chat_request(const api_config_t *config, const char *request_json);
chat_response_t *parse_chat_response(const http_response_t *http_res);

/* HTTP通信模块 */
static CURLcode perform_http_post(const char *url, const char *auth_header,
                                  const char *payload, http_response_t *response);
static char *construct_request_json(const api_config_t *config, 
                                   const chat_request_params_t *params);

/* 工具函数模块 */
void stream_output(const char *text_buffer);
void trim_whitespace(char *string_buffer);

/* 内存安全宏 */
#define SAFE_FREE(ptr) do { free(ptr); (ptr) = NULL; } while(0)

/*------------------------ 配置管理模块实现 ------------------------*/

/**
 * @brief 从指定路径加载配置文件
 * @param config_path 配置文件路径
 * @return 成功返回配置结构体指针，失败返回NULL
 */
api_config_t *load_configuration(const char *config_path)
{
    FILE *config_file = fopen(config_path, "r");
    if (!config_file) {
        perror("Failed to open configuration file");
        return NULL;
    }

    api_config_t *config = calloc(1, sizeof(api_config_t));
    if (!config) {
        fclose(config_file);
        return NULL;
    }

    /* 初始化默认配置值 */
    config->model_name = strdup(DEFAULT_MODEL);
    config->system_prompt = strdup(DEFAULT_SYSTEM_PROMPT);
    if (!config->model_name || !config->system_prompt) {
        perror("Memory allocation failed");
        free_configuration(config);
        fclose(config_file);
        return NULL;
    }

    char config_line[256];
    while (fgets(config_line, sizeof(config_line), config_file)) {
        /* 处理注释和空行 */
        char *comment_start = strchr(config_line, '#');
        if (comment_start) *comment_start = '\0';
        trim_whitespace(config_line);
        if (config_line[0] == '\0') continue;

        /* 解析键值对 */
        char *delimiter = strchr(config_line, '=');
        if (!delimiter) continue;
        *delimiter = '\0';

        char *key = config_line;
        char *value = delimiter + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        /* 动态更新配置字段 */
        char **target_field = NULL;
        if (strcmp(key, "API_KEY") == 0) {
            target_field = &config->api_key;
        } else if (strcmp(key, "BASE_URL") == 0) {
            target_field = &config->base_url;
        } else if (strcmp(key, "MODEL") == 0) {
            target_field = &config->model_name;
        } else if (strcmp(key, "SYSTEM_PROMPT") == 0) {
            target_field = &config->system_prompt;
        }

        if (target_field) {
            char *new_value = strdup(value);
            if (!new_value) {
                perror("Memory allocation failed");
                free_configuration(config);
                fclose(config_file);
                return NULL;
            }
            free(*target_field);  // 释放旧值
            *target_field = new_value;
        }
    }

    if (ferror(config_file)) {
        perror("Error reading configuration file");
        free_configuration(config);
        fclose(config_file);
        return NULL;
    }

    fclose(config_file);
    return config;
}

/**
 * @brief 释放配置结构体占用的内存
 * @param config 要释放的配置结构体指针
 */
void free_configuration(api_config_t *config)
{
    if (config) {
        SAFE_FREE(config->api_key);
        SAFE_FREE(config->base_url);
        SAFE_FREE(config->model_name);
        SAFE_FREE(config->system_prompt);
        SAFE_FREE(config);
    }
}

/**
 * @brief 在标准路径中查找配置文件
 * @return 找到返回有效路径，否则返回NULL
 */
const char *locate_config_file(void)
{
    static const char *config_search_paths[] = {
        "./.adsenv",          // 当前目录
        NULL,                     // 用户目录
        NULL,                     // 用户配置目录
        "/etc/ads/.adsenv"    // 系统级配置
    };

    const char *home_dir = getenv("HOME");
    if (home_dir) {
        static char user_config_path[PATH_MAX];
        static char xdg_config_path[PATH_MAX];
        snprintf(user_config_path, sizeof(user_config_path), "%s/.adsenv", home_dir);
        snprintf(xdg_config_path, sizeof(xdg_config_path), 
                "%s/.config/.adsenv", home_dir);
        config_search_paths[1] = user_config_path;
        config_search_paths[2] = xdg_config_path;
    }

    for (size_t i = 0; i < sizeof(config_search_paths)/sizeof(config_search_paths[0]); ++i) {
        if (config_search_paths[i] && access(config_search_paths[i], R_OK) == 0) {
            return config_search_paths[i];
        }
    }
    return NULL;
}

/**
 * @brief 以JSON格式打印当前配置信息
 * @param config 配置结构体指针
 */
void dump_configuration_json(const api_config_t *config)
{
    cJSON *root_object = cJSON_CreateObject();
    cJSON *config_section = cJSON_AddObjectToObject(root_object, "configuration");
    
    cJSON_AddStringToObject(config_section, "api_key", 
                           config->api_key ? config->api_key : "");
    cJSON_AddStringToObject(config_section, "base_url", 
                           config->base_url ? config->base_url : "");
    cJSON_AddStringToObject(config_section, "model", config->model_name);
    cJSON_AddStringToObject(config_section, "system_prompt", config->system_prompt);
    
    cJSON *constants_section = cJSON_AddObjectToObject(root_object, "constants");
    cJSON_AddStringToObject(constants_section, "DEFAULT_MODEL", DEFAULT_MODEL);
    cJSON_AddStringToObject(constants_section, "DEFAULT_SYSTEM_PROMPT", DEFAULT_SYSTEM_PROMPT);
    cJSON_AddNumberToObject(constants_section, "PATH_MAX", PATH_MAX);
    
    char *json_output = cJSON_Print(root_object);
    if (json_output) {
        printf("%s\n", json_output);
        SAFE_FREE(json_output);
    }
    cJSON_Delete(root_object);
}

/*------------------------ HTTP通信模块实现 ------------------------*/

/**
 * @brief libcurl 写回调函数，用于接收HTTP响应数据
 */
static size_t curl_data_writer(char *buffer, size_t element_size,
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


/**
 * @brief 执行HTTP POST请求
 * @param url 目标URL
 * @param auth_header 认证头信息
 * @param payload 请求体数据
 * @param response 响应存储结构体
 * @return libcurl错误码
 */
static CURLcode perform_http_post(const char *url, const char *auth_header,
                                 const char *payload, http_response_t *response)
{
    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) return CURLE_FAILED_INIT;

    struct curl_slist *header_list = NULL;
    header_list = curl_slist_append(header_list, "Content-Type: application/json");
    header_list = curl_slist_append(header_list, auth_header);

    /* 设置 CURL 选项时添加显式类型转换 */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, 
                    (curl_write_callback)curl_data_writer);  // 关键修改点
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

/**
 * @brief 构建API请求的JSON数据
 * @param config 配置参数
 * @param params 聊天请求参数
 * @return 成功返回JSON字符串，失败返回NULL
 */
static char *construct_request_json(const api_config_t *config,
                                   const chat_request_params_t *params)
{
    cJSON *root_object = cJSON_CreateObject();
    if (!root_object) return NULL;

    if (!cJSON_AddStringToObject(root_object, "model", config->model_name)) goto error;

    cJSON *message_array = cJSON_AddArrayToObject(root_object, "messages");
    if (!message_array) goto error;

    /* 添加系统提示消息 */
    cJSON *system_message = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(system_message, "role", "system") ||
        !cJSON_AddStringToObject(system_message, "content", 
             params->custom_prompt ? params->custom_prompt : config->system_prompt)) {
        cJSON_Delete(system_message);
        goto error;
    }
    cJSON_AddItemToArray(message_array, system_message);

    /* 添加用户查询消息 */
    cJSON *user_message = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(user_message, "role", "user") ||
        !cJSON_AddStringToObject(user_message, "content", params->user_query)) {
        cJSON_Delete(user_message);
        goto error;
    }
    cJSON_AddItemToArray(message_array, user_message);

    /* 禁用流式传输 */
    if (!cJSON_AddFalseToObject(root_object, "stream")) goto error;

    char *json_output = cJSON_PrintUnformatted(root_object);
    cJSON_Delete(root_object);
    return json_output;

error:
    cJSON_Delete(root_object);
    return NULL;
}

/*------------------------ API请求处理模块实现 ------------------------*/

/**
 * @brief 发送聊天请求到API端点
 * @param config 配置参数
 * @param request_json 格式化的JSON请求体
 * @return 成功返回响应结构体，失败返回NULL
 */
http_response_t *execute_chat_request(const api_config_t *config, const char *request_json)
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

/**
 * @brief 解析API响应数据
 * @param http_res HTTP响应结构体
 * @return 成功返回解析后的响应，失败返回NULL
 */
chat_response_t *parse_chat_response(const http_response_t *http_res)
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

    /* 处理API错误信息 */
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

    /* 解析响应内容 */
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

    /* 解析令牌使用情况 */
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

/*------------------------ 工具函数模块实现 ------------------------*/

/**
 * @brief 流式输出文本内容
 * @param text_buffer 要输出的文本缓冲区
 */
void stream_output(const char *text_buffer)
{
    if (!text_buffer) return;

    for (size_t i = 0; text_buffer[i]; ++i) {
        putchar(text_buffer[i]);
        fflush(stdout);
    }
    putchar('\n');
}

/**
 * @brief 去除字符串首尾的空白字符
 * @param string_buffer 要处理的字符串缓冲区
 */
void trim_whitespace(char *string_buffer)
{
    if (!string_buffer) return;

    /* 去除前导空白 */
    char *start_ptr = string_buffer;
    while (isspace((unsigned char)*start_ptr)) start_ptr++;
    memmove(string_buffer, start_ptr, strlen(start_ptr) + 1);

    /* 去除尾部空白 */
    char *end_ptr = string_buffer + strlen(string_buffer) - 1;
    while (end_ptr >= string_buffer && isspace((unsigned char)*end_ptr)) end_ptr--;
    *(end_ptr + 1) = '\0';
}

/*------------------------ 命令行帮助信息 ------------------------*/

/**
 * @brief 打印使用帮助信息
 * @param program_name 程序名称
 * @param output_stream 输出流
 * @param exit_code 退出代码
 */
static void show_usage(const char *program_name, FILE *output_stream, int exit_code)
{
    fprintf(output_stream, "Usage: %s [options]... \"<question>\"\n", program_name);
    fprintf(output_stream, "DeepSeek model command line interface\n\n");
    fprintf(output_stream, "Options:\n");
    fprintf(output_stream, "  -p, --print-config        Print current configuration and exit\n");
    fprintf(output_stream, "  -j, --dry-run             Generate request JSON but do not send\n");
    fprintf(output_stream, "  -t, --show-tokens         Show token usage statistics\n");
    fprintf(output_stream, "  -e, --echo                Echo the user's input question\n");
    fprintf(output_stream, "  -h, --help                Show this help message\n");
    fprintf(output_stream, "\nExamples:\n");
    fprintf(output_stream, "  %s -p                     # Show current configuration\n", program_name);
    fprintf(output_stream, "  %s -j -e \"Your question\"  # Generate request JSON and echo input\n", program_name);
    exit(exit_code);
}

/*------------------------ 命令行参数解析 ------------------------*/

/**
 * @brief 解析命令行参数
 * @param argc 参数个数
 * @param argv 参数数组
 * @param print_config 输出参数：是否打印配置
 * @param show_tokens 输出参数：是否显示令牌统计
 * @param echo_input 输出参数：是否回显输入
 * @param dry_run 输出参数：是否试运行
 * @param user_query 输出参数：用户查询内容
 * @return 成功返回0，失败返回非零
 */
static int parse_cli_arguments(int argc, char **argv,
                              int *print_config, int *show_tokens, int *echo_input,
                              int *dry_run, char **user_query)
{
    static struct option long_options[] = {
        {"print-config",  no_argument, NULL, 'p'},
        {"dry-run",       no_argument, NULL, 'j'},
        {"show-tokens",   no_argument, NULL, 't'},
        {"echo",          no_argument, NULL, 'e'},
        {"help",          no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int option;
    while ((option = getopt_long(argc, argv, "pjteh", long_options, NULL)) != -1) {
        switch (option) {
        case 'p':
            *print_config = 1;
            break;
        case 'j':
            *dry_run = 1;
            break;
        case 't':
            *show_tokens = 1;
            break;
        case 'e':
            *echo_input = 1;
            break;
        case 'h':
            show_usage(argv[0], stdout, EXIT_SUCCESS);
            break;
        default:
            show_usage(argv[0], stderr, EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: Missing required question parameter\n", argv[0]);
        show_usage(argv[0], stderr, EXIT_FAILURE);
    }
    *user_query = argv[optind];
    return 0;
}

/*------------------------ 主程序入口 ------------------------*/

int main(int argc, char **argv)
{
    srand(time(NULL));

    /* 解析命令行参数 */
    int print_config = 0, show_tokens = 0, echo_input = 0, dry_run = 0;
    char *user_question = NULL;

    if (parse_cli_arguments(argc, argv, &print_config, 
                           &show_tokens, &echo_input, &dry_run, &user_question) != 0) {
        return EXIT_FAILURE;
    }

    /* 处理打印配置请求 */
    if (print_config) {
        const char *config_path = locate_config_file();
        if (!config_path) {
            fprintf(stderr, "Configuration file not found\n");
            return EXIT_FAILURE;
        }

        api_config_t *config = load_configuration(config_path);
        if (!config) {
            fprintf(stderr, "Failed to load configuration\n");
            return EXIT_FAILURE;
        }

        dump_configuration_json(config);
        free_configuration(config);
        return EXIT_SUCCESS;
    }

    /* 加载配置文件 */
    const char *config_path = locate_config_file();
    if (!config_path) {
        fprintf(stderr, "Configuration file not found\n");
        return EXIT_FAILURE;
    }

    api_config_t *config = load_configuration(config_path);
    if (!config || !config->api_key || !config->base_url) {
        fprintf(stderr, "Invalid configuration parameters\n");
        free_configuration(config);
        return EXIT_FAILURE;
    }

    /* 回显用户输入 */
    if (echo_input) {
        printf("\nInput: %s\n", user_question);
    }

    /* 构建请求参数 */
    chat_request_params_t request_params = {
        .user_query = user_question,
        .custom_prompt = NULL
    };

    char *request_json = construct_request_json(config, &request_params);
    if (!request_json) {
        fprintf(stderr, "Failed to construct request JSON\n");
        free_configuration(config);
        return EXIT_FAILURE;
    }

    /* 试运行模式只输出JSON */
    if (dry_run) {
        printf("%s\n", request_json);
        SAFE_FREE(request_json);
        free_configuration(config);
        return EXIT_SUCCESS;
    }

    /* 执行API请求 */
    http_response_t *http_response = execute_chat_request(config, request_json);
    SAFE_FREE(request_json);
    if (!http_response) {
        free_configuration(config);
        return EXIT_FAILURE;
    }

    /* 处理API响应 */
    chat_response_t *chat_response = parse_chat_response(http_response);
    if (chat_response && chat_response->content) {
        printf("\nResponse: ");
        stream_output(chat_response->content);
        
        if (show_tokens) {
            printf("\nToken usage:\n  Input: %d\n  Output: %d\n  Total: %d\n",
                  chat_response->input_token_count, 
                  chat_response->output_token_count, 
                  chat_response->total_token_count);
        }
    } else {
        fprintf(stderr, "Failed to get valid response\n");
    }

    /* 资源清理 */
    SAFE_FREE(http_response->payload);
    SAFE_FREE(http_response);
    if (chat_response) {
        SAFE_FREE(chat_response->content);
        SAFE_FREE(chat_response);
    }
    free_configuration(config);

    return EXIT_SUCCESS;
}