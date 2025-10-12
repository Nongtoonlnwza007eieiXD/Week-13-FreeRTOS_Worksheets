#include <stdio.h>
#include <stdarg.h>
#include "custom_logger.h"

// ANSI Color Codes
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_YELLOW  "33"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_CYAN    "36"

void custom_log(custom_log_level_t level, const char* tag, const char* format, ...)
{
    const char* color = LOG_COLOR_CYAN;
    const char* level_text = "INFO";

    switch (level) {
        case CUSTOM_LOG_ERROR: color = LOG_COLOR_RED;    level_text = "ERROR"; break;
        case CUSTOM_LOG_WARN:  color = LOG_COLOR_YELLOW; level_text = "WARN";  break;
        case CUSTOM_LOG_INFO:  color = LOG_COLOR_GREEN;  level_text = "INFO";  break;
        case CUSTOM_LOG_DEBUG: color = LOG_COLOR_BLUE;   level_text = "DEBUG"; break;
        default: break;
    }

    va_list args;
    va_start(args, format);

    // ✅ ใช้ printf แบบนี้เพื่อรองรับตัวแปรสี (ไม่ใช้ macro ต่อ string)
    printf("\033[1;%sm[%s] %s: \033[0m", color, level_text, tag);
    vprintf(format, args);
    printf("\n");

    va_end(args);
}
