/* log.h — Simple kernel logging facility */
#pragma once
#include <stdarg.h>
#include "vga.h"

/* Log levels */
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4
} log_level_t;

/* Current log level - can be changed at runtime */
extern log_level_t current_log_level;

/* Initialize the logging system */
void log_init(void);

/* Set the current log level */
void log_set_level(log_level_t level);

/* Core logging function */
void log_printf(log_level_t level, const char *fmt, ...);

/* Convenience macros for each log level */
#define LOG_ERROR(fmt, ...) \
    do { \
        if (current_log_level >= LOG_LEVEL_ERROR) { \
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK); \
            vga_printf("[ERROR] " fmt "\n", ##__VA_ARGS__); \
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); \
        } \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (current_log_level >= LOG_LEVEL_WARN) { \
            vga_set_color(VGA_YELLOW, VGA_BLACK); \
            vga_printf("[WARN] " fmt "\n", ##__VA_ARGS__); \
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); \
        } \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (current_log_level >= LOG_LEVEL_INFO) { \
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK); \
            vga_printf("[INFO] " fmt "\n", ##__VA_ARGS__); \
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (current_log_level >= LOG_LEVEL_DEBUG) { \
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK); \
            vga_printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); \
        } \
    } while (0)
