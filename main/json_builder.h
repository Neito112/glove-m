#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include "config.h"
#include "json_util.h"
#include "tools.h"
#include <stdbool.h>
#include <stddef.h>

// A growable string that writes into a fixed buffer.
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} sbuf_t;

void sbuf_init(sbuf_t *sb, char *buf, size_t cap);
void sbuf_putc(sbuf_t *sb, char c);
void sbuf_puts(sbuf_t *sb, const char *s);
void sbuf_put_json_string(sbuf_t *sb, const char *s);
void sbuf_put_json_raw(sbuf_t *sb, const char *raw);
void sbuf_put_uint(sbuf_t *sb, unsigned value);

bool build_openai_request_into(
    sbuf_t *sb,
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const tool_def_t *tools,
    int tool_count);

bool build_anthropic_request_into(
    sbuf_t *sb,
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const tool_def_t *tools,
    int tool_count);

#endif // JSON_BUILDER_H
