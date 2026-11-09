#include "json_util.h"
#include "json_builder.h"
#include "config.h"
#include "tools.h"
#include "user_tools.h"
#include "llm.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "json";

// Keep parsed response tree alive for tool_input access
static cJSON *s_parsed_response = NULL;

static bool parse_anthropic_response(
    cJSON *root,
    char *text_out,
    size_t text_out_len,
    char *tool_name_out,
    size_t tool_name_len,
    char *tool_id_out,
    size_t tool_id_len,
    cJSON **tool_input_out)
{
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!content || !cJSON_IsArray(content)) {
        ESP_LOGE(TAG, "No content array in response");
        return false;
    }

    cJSON *block;
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) continue;

        if (strcmp(type->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text)) {
                strncpy(text_out, text->valuestring, text_out_len - 1);
                text_out[text_out_len - 1] = '\0';
            }
        } else if (strcmp(type->valuestring, "tool_use") == 0) {
            cJSON *name = cJSON_GetObjectItem(block, "name");
            cJSON *id = cJSON_GetObjectItem(block, "id");
            cJSON *input = cJSON_GetObjectItem(block, "input");

            if (name && cJSON_IsString(name)) {
                strncpy(tool_name_out, name->valuestring, tool_name_len - 1);
                tool_name_out[tool_name_len - 1] = '\0';
            }
            if (id && cJSON_IsString(id)) {
                strncpy(tool_id_out, id->valuestring, tool_id_len - 1);
                tool_id_out[tool_id_len - 1] = '\0';
            }
            if (input) {
                *tool_input_out = input;
            }
        }
    }

    return true;
}

static bool parse_openai_response(    cJSON *root,
    char *text_out,
    size_t text_out_len,
    char *tool_name_out,
    size_t tool_name_len,
    char *tool_id_out,
    size_t tool_id_len,
    cJSON **tool_input_out)
{
    // OpenAI: choices[0].message
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        ESP_LOGE(TAG, "No choices in response");
        return false;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) {
        ESP_LOGE(TAG, "No message in choice");
        return false;
    }

    // Check for text content
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && cJSON_IsString(content)) {
        strncpy(text_out, content->valuestring, text_out_len - 1);
        text_out[text_out_len - 1] = '\0';
    }

    // Check for tool_calls
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
        cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);

        cJSON *id = cJSON_GetObjectItem(tc, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(tool_id_out, id->valuestring, tool_id_len - 1);
            tool_id_out[tool_id_len - 1] = '\0';
        }

        cJSON *func = cJSON_GetObjectItem(tc, "function");
        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            if (name && cJSON_IsString(name)) {
                strncpy(tool_name_out, name->valuestring, tool_name_len - 1);
                tool_name_out[tool_name_len - 1] = '\0';
            }

            // Parse arguments string into JSON
            cJSON *args = cJSON_GetObjectItem(func, "arguments");
            if (args && cJSON_IsString(args)) {
                cJSON *parsed_args = cJSON_Parse(args->valuestring);
                if (!parsed_args) {
                    parsed_args = cJSON_CreateObject();
                }
                if (parsed_args) {
                    cJSON_AddItemToObject(tc, "_parsed_arguments", parsed_args);
                    *tool_input_out = parsed_args;
                }
            }
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

size_t json_build_request_into(
    char *buf,
    size_t buf_len,
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const tool_def_t *tools,
    int tool_count)
{
    if (!buf || buf_len == 0) {
        return 0;
    }

    sbuf_t sb;
    sbuf_init(&sb, buf, buf_len);

    bool ok;
    if (llm_is_openai_format()) {
        ok = build_openai_request_into(&sb, system_prompt, history, history_len,
                                        user_message, tools, tool_count);
    } else {
        ok = build_anthropic_request_into(&sb, system_prompt, history, history_len,
                                           user_message, tools, tool_count);
    }

    if (!ok) {
        ESP_LOGE(TAG, "Request does not fit in %u bytes, heap free=%u",
                 (unsigned)buf_len, (unsigned)esp_get_free_heap_size());
        buf[0] = '\0';
        return 0;
    }

    ESP_LOGD(TAG, "Built request: %u bytes", (unsigned)sb.len);
    return sb.len;
}

bool json_parse_response(
    const char *response_json,
    char *text_out,
    size_t text_out_len,
    char *tool_name_out,
    size_t tool_name_len,
    char *tool_id_out,
    size_t tool_id_len,
    cJSON **tool_input_out)
{
    // Free any previous parsed response
    json_free_parsed_response();

    text_out[0] = '\0';
    tool_name_out[0] = '\0';
    tool_id_out[0] = '\0';
    *tool_input_out = NULL;

    s_parsed_response = cJSON_Parse(response_json);
    if (!s_parsed_response) {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        return false;
    }

    // Check for error (both APIs use similar format)
    cJSON *error = cJSON_GetObjectItem(s_parsed_response, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            snprintf(text_out, text_out_len, "API Error: %s", msg->valuestring);
        } else {
            snprintf(text_out, text_out_len, "API Error (unknown)");
        }
        return true;
    }

    // Parse based on format
    if (llm_is_openai_format()) {
        return parse_openai_response(s_parsed_response, text_out, text_out_len,
                                      tool_name_out, tool_name_len,
                                      tool_id_out, tool_id_len, tool_input_out);
    } else {
        return parse_anthropic_response(s_parsed_response, text_out, text_out_len,
                                         tool_name_out, tool_name_len,
                                         tool_id_out, tool_id_len, tool_input_out);
    }
}

void json_free_parsed_response(void)
{
    if (s_parsed_response) {
        cJSON_Delete(s_parsed_response);
        s_parsed_response = NULL;
    }
}
