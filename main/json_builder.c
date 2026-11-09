// Request builders that write JSON straight into a caller-provided buffer.
//
// Building the request with cJSON needs three heap allocations at once: the
// parsed tree, the internal print buffer and the returned copy. On a device
// with a small heap that peak does not fit, so the request is written by hand
// instead. Only the response side still uses cJSON.

#include "json_builder.h"
#include "config.h"
#include "tools.h"
#include "user_tools.h"
#include "llm.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "json";

void sbuf_init(sbuf_t *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    sb->overflow = false;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

void sbuf_putc(sbuf_t *sb, char c)
{
    if (sb->len + 1 >= sb->cap) {
        sb->overflow = true;
        return;
    }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sbuf_puts(sbuf_t *sb, const char *s)
{
    if (!s) {
        return;
    }
    while (*s) {
        sbuf_putc(sb, *s++);
    }
}

// Write a JSON string literal, escaping what the specification requires.
void sbuf_put_json_string(sbuf_t *sb, const char *s)
{
    sbuf_putc(sb, '"');
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            switch (*p) {
                case '"':
                    sbuf_puts(sb, "\\\"");
                    break;
                case '\\':
                    sbuf_puts(sb, "\\\\");
                    break;
                case '\b':
                    sbuf_puts(sb, "\\b");
                    break;
                case '\f':
                    sbuf_puts(sb, "\\f");
                    break;
                case '\n':
                    sbuf_puts(sb, "\\n");
                    break;
                case '\r':
                    sbuf_puts(sb, "\\r");
                    break;
                case '\t':
                    sbuf_puts(sb, "\\t");
                    break;
                default:
                    if (*p < 0x20) {
                        char esc[7];
                        snprintf(esc, sizeof(esc), "\\u%04x", *p);
                        sbuf_puts(sb, esc);
                    } else {
                        sbuf_putc(sb, (char)*p);
                    }
                    break;
            }
        }
    }
    sbuf_putc(sb, '"');
}

// Write raw JSON text as-is. Used for the tool schemas, which are already
// valid JSON literals.
void sbuf_put_json_raw(sbuf_t *sb, const char *raw)
{
    sbuf_puts(sb, raw ? raw : "{}");
}

void sbuf_put_uint(sbuf_t *sb, unsigned value)
{
    char num[12];
    snprintf(num, sizeof(num), "%u", value);
    sbuf_puts(sb, num);
}

static bool history_has_prior_tool_use(
    const conversation_msg_t *history,
    int index,
    const char *tool_id)
{
    if (!tool_id || tool_id[0] == '\0') {
        return false;
    }
    for (int i = 0; i < index; i++) {
        if (history[i].is_tool_use && strcmp(history[i].tool_id, tool_id) == 0) {
            return true;
        }
    }
    return false;
}

// Append the built-in tool definitions in the OpenAI function format.
static void append_openai_builtin_tools(sbuf_t *sb, const tool_def_t *tools, int tool_count)
{
    for (int i = 0; i < tool_count; i++) {
        sbuf_puts(sb, "{\"type\":\"function\",\"function\":{\"name\":");
        sbuf_put_json_string(sb, tools[i].name);
        sbuf_puts(sb, ",\"description\":");
        sbuf_put_json_string(sb, tools[i].description);
        sbuf_puts(sb, ",\"parameters\":");
        sbuf_put_json_raw(sb, tools[i].input_schema_json);
        sbuf_puts(sb, "}}");
        if (i + 1 < tool_count) {
            sbuf_putc(sb, ',');
        }
    }
}

// Append the built-in tool definitions in the Anthropic format.
static void append_anthropic_builtin_tools(sbuf_t *sb, const tool_def_t *tools, int tool_count)
{
    for (int i = 0; i < tool_count; i++) {
        sbuf_puts(sb, "{\"name\":");
        sbuf_put_json_string(sb, tools[i].name);
        sbuf_puts(sb, ",\"description\":");
        sbuf_put_json_string(sb, tools[i].description);
        sbuf_puts(sb, ",\"input_schema\":");
        sbuf_put_json_raw(sb, tools[i].input_schema_json);
        sbuf_putc(sb, '}');
        if (i + 1 < tool_count) {
            sbuf_putc(sb, ',');
        }
    }
}

// Append the user-defined tools. Their schema is always an empty object.
static int append_user_tools(sbuf_t *sb, const char *separator)
{
    user_tool_t user_tools_arr[MAX_DYNAMIC_TOOLS];
    int loaded = user_tools_get_all(user_tools_arr, MAX_DYNAMIC_TOOLS);
    for (int i = 0; i < loaded; i++) {
        sbuf_puts(sb, separator);
        sbuf_puts(sb, "{\"name\":");
        sbuf_put_json_string(sb, user_tools_arr[i].name);
        sbuf_puts(sb, ",\"description\":");
        sbuf_put_json_string(sb, user_tools_arr[i].description);
        sbuf_puts(sb, ",\"input_schema\":{\"type\":\"object\",\"properties\":{}}}");
    }
    return loaded;
}

// Free-only OpenRouter fallbacks. Every entry is a zero-cost model, so the
// router never spends credits even when the primary model is unavailable.
// OpenRouter rejects a "models" array with more than three entries.
static const char *const s_openrouter_free_models[] = {
    "cohere/north-mini-code:free",
    "liquid/lfm-2.5-2.6b:free",
    "recursive/flint-v1.0:free",
};

static void append_openrouter_free_models(sbuf_t *sb)
{
    if (llm_get_backend() != LLM_BACKEND_OPENROUTER || !llm_model_is_free()) {
        return;
    }

    sbuf_puts(sb, ",\"models\":[");
    for (size_t i = 0; i < sizeof(s_openrouter_free_models) / sizeof(s_openrouter_free_models[0]); i++) {
        if (i > 0) {
            sbuf_putc(sb, ',');
        }
        sbuf_put_json_string(sb, s_openrouter_free_models[i]);
    }
    sbuf_putc(sb, ']');
}

// Build the OpenAI-compatible request body.
bool build_openai_request_into(
    sbuf_t *sb,
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const tool_def_t *tools,
    int tool_count)
{
    sbuf_puts(sb, "{\"model\":");
    sbuf_put_json_string(sb, llm_get_model());

    if (llm_get_backend() == LLM_BACKEND_OPENAI) {
        // GPT-5 chat-completions models reject max_tokens.
        sbuf_puts(sb, ",\"max_completion_tokens\":");
    } else {
        sbuf_puts(sb, ",\"max_tokens\":");
    }
    sbuf_put_uint(sb, LLM_MAX_TOKENS);

    append_openrouter_free_models(sb);

    sbuf_puts(sb, ",\"messages\":[{\"role\":\"system\",\"content\":");
    sbuf_put_json_string(sb, system_prompt);
    sbuf_putc(sb, '}');

    for (int i = 0; i < history_len; i++) {
        if (history[i].is_tool_use) {
            sbuf_puts(sb, ",{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":");
            sbuf_put_json_string(sb, history[i].tool_id);
            sbuf_puts(sb, ",\"type\":\"function\",\"function\":{\"name\":");
            sbuf_put_json_string(sb, history[i].tool_name);
            sbuf_puts(sb, ",\"arguments\":");
            sbuf_put_json_raw(sb, history[i].content);
            sbuf_puts(sb, "}}]}");
        } else if (history[i].is_tool_result) {
            if (!history_has_prior_tool_use(history, i, history[i].tool_id)) {
                ESP_LOGW(TAG, "Skipping orphan tool_result in history[%d] (id=%s)",
                         i, history[i].tool_id);
                continue;
            }
            sbuf_puts(sb, ",{\"role\":\"tool\",\"tool_call_id\":");
            sbuf_put_json_string(sb, history[i].tool_id);
            sbuf_puts(sb, ",\"content\":");
            sbuf_put_json_string(sb, history[i].content);
            sbuf_putc(sb, '}');
        } else {
            sbuf_puts(sb, ",{\"role\":");
            sbuf_put_json_string(sb, history[i].role);
            sbuf_puts(sb, ",\"content\":");
            sbuf_put_json_string(sb, history[i].content);
            sbuf_putc(sb, '}');
        }
    }

    if (user_message && user_message[0] != '\0') {
        sbuf_puts(sb, ",{\"role\":\"user\",\"content\":");
        sbuf_put_json_string(sb, user_message);
        sbuf_putc(sb, '}');
    }

    sbuf_puts(sb, "]");

    if (tool_count > 0 || user_tools_count() > 0) {
        sbuf_puts(sb, ",\"tools\":[");
        append_openai_builtin_tools(sb, tools, tool_count);
        if (tool_count > 0 && user_tools_count() > 0) {
            sbuf_putc(sb, ',');
        }
        append_user_tools(sb, ",");
        sbuf_putc(sb, ']');
    }

    sbuf_putc(sb, '}');
    return !sb->overflow;
}

// Build the Anthropic request body.
bool build_anthropic_request_into(
    sbuf_t *sb,
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const tool_def_t *tools,
    int tool_count)
{
    sbuf_puts(sb, "{\"model\":");
    sbuf_put_json_string(sb, llm_get_model());
    sbuf_puts(sb, ",\"max_tokens\":");
    sbuf_put_uint(sb, LLM_MAX_TOKENS);
    sbuf_puts(sb, ",\"system\":");
    sbuf_put_json_string(sb, system_prompt);
    sbuf_puts(sb, ",\"messages\":[");

    bool first = true;
    for (int i = 0; i < history_len; i++) {
        if (history[i].is_tool_use) {
            if (!first) {
                sbuf_putc(sb, ',');
            }
            first = false;
            sbuf_puts(sb, "{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":");
            sbuf_put_json_string(sb, history[i].tool_id);
            sbuf_puts(sb, ",\"name\":");
            sbuf_put_json_string(sb, history[i].tool_name);
            sbuf_puts(sb, ",\"input\":");
            sbuf_put_json_raw(sb, history[i].content);
            sbuf_puts(sb, "}]}");
        } else if (history[i].is_tool_result) {
            if (!history_has_prior_tool_use(history, i, history[i].tool_id)) {
                ESP_LOGW(TAG, "Skipping orphan tool_result in history[%d] (id=%s)",
                         i, history[i].tool_id);
                continue;
            }
            if (!first) {
                sbuf_putc(sb, ',');
            }
            first = false;
            sbuf_puts(sb, "{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":");
            sbuf_put_json_string(sb, history[i].tool_id);
            sbuf_puts(sb, ",\"content\":");
            sbuf_put_json_string(sb, history[i].content);
            sbuf_puts(sb, "}]}");
        } else {
            if (!first) {
                sbuf_putc(sb, ',');
            }
            first = false;
            sbuf_puts(sb, "{\"role\":");
            sbuf_put_json_string(sb, history[i].role);
            sbuf_puts(sb, ",\"content\":");
            sbuf_put_json_string(sb, history[i].content);
            sbuf_putc(sb, '}');
        }
    }

    if (user_message && user_message[0] != '\0') {
        if (!first) {
            sbuf_putc(sb, ',');
        }
        sbuf_puts(sb, "{\"role\":\"user\",\"content\":");
        sbuf_put_json_string(sb, user_message);
        sbuf_putc(sb, '}');
    }

    sbuf_putc(sb, ']');

    if (tool_count > 0 || user_tools_count() > 0) {
        sbuf_puts(sb, ",\"tools\":[");
        append_anthropic_builtin_tools(sb, tools, tool_count);
        if (tool_count > 0 && user_tools_count() > 0) {
            sbuf_putc(sb, ',');
        }
        append_user_tools(sb, ",");
        sbuf_putc(sb, ']');
    }

    sbuf_putc(sb, '}');
    return !sb->overflow;
}
