#ifndef LLM_H
#define LLM_H

#include "config.h"
#include "esp_err.h"
#include <stdbool.h>

// Initialize the LLM HTTP client
esp_err_t llm_init(void);

// Send a request to LLM API
// request_json: the complete API request body (format depends on backend)
// response_buf: buffer to store the response
// response_buf_size: size of response buffer
// Returns ESP_OK on success
esp_err_t llm_request(const char *request_json, char *response_buf, size_t response_buf_size);

// Check if we're in stub mode (QEMU testing)
bool llm_is_stub_mode(void);

// Get current backend type
llm_backend_t llm_get_backend(void);

// Get API URL for current backend
const char *llm_get_api_url(void);

// Get default model for current backend
const char *llm_get_default_model(void);

// Get current model (user-configured or default)
const char *llm_get_model(void);

// Check if backend uses OpenAI-compatible format (OpenAI, OpenRouter, Ollama)
bool llm_is_openai_format(void);

// True when the active OpenRouter model is a zero-cost model (either the
// "openrouter/free" router or a ":free" model id), so the request can pin the
// free-only fallback list.
bool llm_model_is_free(void);

// -----------------------------------------------------------------------------
// Model health tracking
//
// A model that keeps failing or answering too slowly is dropped from the
// rotation for the rest of the boot, so the next request uses another one.
// -----------------------------------------------------------------------------

// Record a successful call and its round-trip time. Returns true when the
// model was fast enough to stay in the rotation.
bool llm_model_report_success(uint32_t elapsed_ms);

// Record a failed call. Returns true when the model was dropped from the
// rotation because it failed too many times.
bool llm_model_report_failure(void);

// Switch to the next model in the rotation. Returns false when every model
// was already dropped, in which case the rotation is reset and retried.
bool llm_model_rotate(void);

// True when at least one other model is available after the current one.
bool llm_model_has_alternative(void);

// Number of models still usable in the rotation.
int llm_model_available_count(void);

#if CONFIG_ZCLAW_STUB_LLM
// Host-test helper: indicates whether an API key is currently loaded in runtime state.
bool llm_stub_has_api_key_for_test(void);
#endif

#endif // LLM_H
