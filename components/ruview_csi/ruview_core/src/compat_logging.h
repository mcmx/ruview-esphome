/**
 * @file compat_logging.h
 * @brief Logging compatibility shim for ruview_core.
 *
 * When built inside ESPHome, ESP_LOG* macros are already available via
 * the ESP-IDF framework layer.  This header exists so that ruview_core
 * source files can be compiled with a consistent include, and so that
 * log tags can be overridden if needed in the future.
 */

#ifndef COMPAT_LOGGING_H
#define COMPAT_LOGGING_H

#include "esp_log.h"

/*
 * No additional wrappers needed — ESP-IDF logging macros are available
 * in the ESPHome ESP-IDF build environment.  If a non-ESP-IDF target
 * is ever added, replace these with printf-based equivalents.
 */

#endif /* COMPAT_LOGGING_H */
