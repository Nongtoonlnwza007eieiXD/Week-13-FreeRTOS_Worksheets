#pragma once
#include <stdarg.h>

typedef enum {
    CUSTOM_LOG_ERROR,
    CUSTOM_LOG_WARN,
    CUSTOM_LOG_INFO,
    CUSTOM_LOG_DEBUG
} custom_log_level_t;

void custom_log(custom_log_level_t level, const char* tag, const char* format, ...);
