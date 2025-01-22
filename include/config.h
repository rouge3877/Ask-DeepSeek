/**
 * @file config.h
 * @brief Configuration management module
 * @note Contains configuration loading/parsing functions
 * @author Rouge Lin
 * @date 2025-01-23
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <cjson/cJSON.h>

/* Configuration constants */
/**
 * @def PATH_MAX
 * @brief Maximum file path length
 * @note If the PATH_MAX macro is not defined, set it to 4096
 */
#ifndef PATH_MAX
# define PATH_MAX 4096                /* Maximum file path length */
#endif

/**
 * @def DEFAULT_MODEL
 * @brief Default model name
 * @note If the DEFAULT_MODEL macro is not defined, set it to "deepseek-chat"
 */
#ifndef DEFAULT_MODEL
# define DEFAULT_MODEL "deepseek-chat" /* Default model name */
#endif

/**
 * @def DEFAULT_SYSTEM_PROMPT
 * @brief Default system prompt
 * @note If the DEFAULT_SYSTEM_PROMPT macro is not defined, set it to "You are a helpful assistant."
 */
#ifndef DEFAULT_SYSTEM_PROMPT
# define DEFAULT_SYSTEM_PROMPT "You are a helpful assistant." /* Default system prompt */
#endif

/**
 * @struct api_config_t
 * @brief Structure that stores API configuration parameters
 * @var api_key API access key
 * @var base_url Base URL of the API endpoint
 * @var model_name Name of the model to use
 * @var system_prompt System-level prompt information
 */
typedef struct {
    char *api_key;       /**< API access key */
    char *base_url;      /**< Base URL of the API endpoint */
    char *model_name;    /**< Name of the model to use */
    char *system_prompt; /**< System-level prompt information */
} api_config_t;

/**
 * @brief Load configuration from file
 * @param config_path Path to the configuration file
 * @return Pointer to the configuration structure
 * @note Configuration file format is key-value pairs, supporting the following keys:
 *      - API_KEY: DeepSeek API access key
 *      - BASE_URL: Base URL of the DeepSeek API endpoint
 *      - MODEL: Name of the model to use
 *      - SYSTEM_PROMPT: System-level prompt information
 * @note If the path is empty, attempts to locate the file from default locations
 */
api_config_t *load_configuration(const char *config_path);

/**
 * @brief Free configuration structure
 * @param config Pointer to the configuration structure
 * @note Frees the configuration structure and its member variables
 * @note Does nothing if a NULL pointer is passed
 */
void free_configuration(api_config_t *config);

/**
 * @brief Locate the configuration file
 * @param void
 * @return Path to the configuration file as a string
 * @note Attempts to locate the configuration file from the following paths:
 *      - .adsenv file in the current directory
 *      - .adsenv file in the user's home directory
 *      - .adsenv file in the user's .config directory
 *      - .adsenv file in the system-wide /etc/ads directory
 */
const char *locate_config_file(void);

/**
 * @brief Print configuration in JSON format
 * @param config Pointer to the configuration structure
 * @return void
 * @note Outputs the configuration structure as a JSON formatted string
 */
void dump_configuration_json(const api_config_t *config);

#endif