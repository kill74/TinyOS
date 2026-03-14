/* log.c — Simple kernel logging facility implementation */

#include "log.h"

/* Global log level - defaults to INFO */
log_level_t current_log_level = LOG_LEVEL_INFO;

void log_init(void) {
    /* Log system initialized at INFO level by default */
    LOG_INFO("Logging system initialized");
}

void log_set_level(log_level_t level) {
    current_log_level = level;
    LOG_INFO("Log level set to %d", level);
}