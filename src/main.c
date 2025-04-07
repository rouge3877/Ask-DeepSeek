/**
 * @file main.c
 * @brief Ask-DeepSeek CLI main program
 * @note Entry point and command line handling
 * @author Rouge Lin
 * @date 2025-04-07
 */

#include "config.h"
#include "http_client.h"
#include "api_handler.h"
#include "stream_handler.h"
#include "utils.h"
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>


#define MAX_INPUT_SIZE 1024*1024 // 1MB

/**
 * @brief Print usage instructions
 * @param program_name Program name
 * @param output_stream Output stream
 * @param exit_code Exit code
 * @return void
 */
static void show_usage (const char *program_name, FILE *output_stream, int exit_code);

/**
 * @brief Parse command-line arguments
 * @param argc Number of arguments
 * @param argv List of arguments
 * @param print_config Print configuration flag
 * @param show_tokens Show token statistics flag
 * @param echo_input Echo input flag
 * @param dry_run Dry run flag
 * @param store_forward Non-streaming mode flag
 * @param user_query User question string
 * @return 0 on success, -1 on failure
 */
static int parse_cli_arguments (int argc, char **argv,
                                int *print_config, int *show_tokens, int *echo_input,
                                int *dry_run, int *store_forward, char **user_query);

/**
 * @brief Read all content from standard input
 * @return Dynamically allocated string with stdin content, NULL on failure
 */
static char *read_stdin(void);

/*------------------------ Main program entry point ------------------------*/

/**
 * @brief main: Program entry point
 * @param argc Number of arguments
 * @param argv List of arguments
 * @return 0 on success, 1 on failure
 */
int
main (int argc, char **argv)
{
    srand(time(NULL));

    int print_config = 0, show_tokens = 0, echo_input = 0;
    int dry_run = 0, store_forward = 0;
    char *user_question = NULL;
    char *stdin_input = NULL;

    if (parse_cli_arguments(argc, argv, &print_config, 
                           &show_tokens, &echo_input, &dry_run, 
                           &store_forward, &user_question) != 0) {
        return EXIT_FAILURE;
    }

    // if use - , read from stdin
    if (strcmp(user_question, "-") == 0) {
        stdin_input = read_stdin();
        if (!stdin_input) {
            fprintf(stderr, "Failed to read from standard input\n");
            return EXIT_FAILURE;
        }
        user_question = stdin_input;
    }

    if (print_config) {
        const char *config_path = locate_config_file();
        if (!config_path) {
            fprintf(stderr, "Configuration file not found\n");
            SAFE_FREE(stdin_input);
            return EXIT_FAILURE;
        }

        api_config_t *config = load_configuration(config_path);
        if (!config) {
            fprintf(stderr, "Failed to load configuration\n");
            SAFE_FREE(stdin_input);
            return EXIT_FAILURE;
        }

        dump_configuration_json(config);
        free_configuration(config);
        SAFE_FREE(stdin_input);
        return EXIT_SUCCESS;
    }

    const char *config_path = locate_config_file();
    if (!config_path) {
        fprintf(stderr, "Configuration file not found\n");
        SAFE_FREE(stdin_input);
        return EXIT_FAILURE;
    }

    api_config_t *config = load_configuration(config_path);
    if (!config || !config->api_key || !config->base_url) {
        fprintf(stderr, "Invalid configuration parameters\n");
        free_configuration(config);
        SAFE_FREE(stdin_input);
        return EXIT_FAILURE;
    }

    if (echo_input) {
        printf("\nInput: %s\n", user_question);
    }

    chat_request_params_t request_params = {
        .user_query = user_question,
        .custom_prompt = NULL
    };

    int stream_enabled = !store_forward;
    char *request_json = construct_request_json(config, &request_params, stream_enabled);
    if (!request_json) {
        fprintf(stderr, "Failed to construct request JSON\n");
        free_configuration(config);
        SAFE_FREE(stdin_input);
        return EXIT_FAILURE;
    }

    if (dry_run) {
        printf("%s\n", request_json);
        SAFE_FREE(request_json);
        free_configuration(config);
        SAFE_FREE(stdin_input);
        return EXIT_SUCCESS;
    }

    if (stream_enabled) {
        fflush(stdout);
        int result = execute_streaming_request(config, request_json, show_tokens);
        printf("\n");
        SAFE_FREE(request_json);
        free_configuration(config);
        SAFE_FREE(stdin_input);
        return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        http_response_t *http_response = execute_chat_request(config, request_json);
        SAFE_FREE(request_json);
        if (!http_response) {
            free_configuration(config);
            SAFE_FREE(stdin_input);
            return EXIT_FAILURE;
        }

        chat_response_t *chat_response = parse_chat_response(http_response);
        if (chat_response && chat_response->content) {
            printf("%s", chat_response->content);
            printf("\n");
            
            if (show_tokens) {
                printf("\nToken usage:\n  Input: %d\n  Output: %d\n  Total: %d\n",
                      chat_response->input_token_count, 
                      chat_response->output_token_count, 
                      chat_response->total_token_count);
            }
        } else {
            fprintf(stderr, "Failed to get valid response\n");
        }

        SAFE_FREE(http_response->payload);
        SAFE_FREE(http_response);
        if (chat_response) {
            SAFE_FREE(chat_response->content);
            SAFE_FREE(chat_response);
        }
        free_configuration(config);
        SAFE_FREE(stdin_input);
    }

    return EXIT_SUCCESS;
}

/*------------------------ Command line help info ------------------------*/

static void 
show_usage (const char *program_name, FILE *output_stream, int exit_code)
{
    fprintf(output_stream, "Usage: %s [options]... \"<question>\"\n", program_name);
    fprintf(output_stream, "DeepSeek model command line interface\n\n");
    fprintf(output_stream, "Options:\n");
    fprintf(output_stream, "  -p, --print-config        Print current configuration and exit\n");
    fprintf(output_stream, "  -j, --dry-run             Generate request JSON but do not send\n");
    fprintf(output_stream, "  -t, --show-tokens         Show token usage statistics\n");
    fprintf(output_stream, "  -e, --echo                Echo the user's input question\n");
    fprintf(output_stream, "  -s, --store-forward       Use non-streaming mode\n");
    fprintf(output_stream, "  -h, --help                Show this help message\n");
    fprintf(output_stream, "\nExamples:\n");
    fprintf(output_stream, "  %s -p                     # Show current configuration\n", program_name);
    fprintf(output_stream, "  %s -j -e \"Your question\"  # Generate request JSON and echo input\n", program_name);
    fprintf(output_stream, "  %s - < input.txt          # Read question from standard input\n", program_name);
    exit(exit_code);
}

/*------------------------ Command line argument parsing ------------------------*/

static int
parse_cli_arguments (int argc, char **argv,
                     int *print_config, int *show_tokens, int *echo_input,
                     int *dry_run, int *store_forward, char **user_query)
{
    static struct option long_options[] = {
        {"print-config",  no_argument, NULL, 'p'},
        {"dry-run",       no_argument, NULL, 'j'},
        {"show-tokens",   no_argument, NULL, 't'},
        {"echo",          no_argument, NULL, 'e'},
        {"store-forward", no_argument, NULL, 's'},
        {"help",          no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int option;
    while ((option = getopt_long(argc, argv, "pjtehsh", long_options, NULL)) != -1) {
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
        case 's':
            *store_forward = 1;
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

/*------------------------ Read from standard input ------------------------*/

static char *
read_stdin (void)
{
    size_t capacity = MAX_INPUT_SIZE;
    size_t size = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return NULL;

    while (1) {
        size_t bytes_read = fread(buffer + size, 1, capacity - size, stdin);
        size += bytes_read;

        if (bytes_read < capacity - size) {
            if (feof(stdin)) {
                break;
            } else {
                free(buffer);
                return NULL;
            }
        }

        if (size == capacity) {
            capacity *= 2;
            char *temp = realloc(buffer, capacity);
            if (!temp) {
                free(buffer);
                return NULL;
            }
            buffer = temp;
        }
    }

    buffer[size] = '\0';
    return buffer;
}
