/**
 * @file config.c
 * @brief Configuration management implementation
 * @author Rouge Lin
 * @date 2025-01-23
 */

#include "config.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*------------------------ Configuration management module implementation ------------------------*/

api_config_t *
load_configuration (const char *config_path)
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
        char *comment_start = strchr(config_line, '#');
        if (comment_start) *comment_start = '\0';
        trim_whitespace(config_line);
        if (config_line[0] == '\0') continue;

        char *delimiter = strchr(config_line, '=');
        if (!delimiter) continue;
        *delimiter = '\0';

        char *key = config_line;
        char *value = delimiter + 1;
        trim_whitespace(key);
        trim_whitespace(value);

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
            free(*target_field);
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

void
free_configuration (api_config_t *config)
{
    if (config) {
        SAFE_FREE(config->api_key);
        SAFE_FREE(config->base_url);
        SAFE_FREE(config->model_name);
        SAFE_FREE(config->system_prompt);
        SAFE_FREE(config);
    }
}

const char *
locate_config_file (void)
{
    static const char *config_search_paths[] = {
        "./.adsenv",
        NULL,
        NULL,
        "/etc/ads/.adsenv"
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

void
dump_configuration_json (const api_config_t *config)
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
