/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-logger.h
 * @brief Simple file logger with rotation for fcitx5-lotus-server
 *
 * Features:
 * - Thread-safe logging
 * - Automatic file rotation (max 10MB, keep 5 files)
 * - Timestamp and level formatting
 * - Configurable log levels
 */

#ifndef _LOTUS_LOGGER_H_
#define _LOTUS_LOGGER_H_

#include <fstream>
#include <string>
#include <mutex>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    NONE
};

class LotusLogger {
  public:
    /**
     * @brief Constructor
     * @param log_file Path to log file
     * @param max_size Maximum file size before rotation (bytes)
     * @param max_files Maximum number of backup files to keep
     * @param level Minimum log level to output
     */
    LotusLogger(const std::string& log_file = "/tmp/fcitx5-lotus-server.log",
                size_t             max_size = 10 * 1024 * 1024, // 10MB
                size_t max_files = 5, LogLevel level = LogLevel::INFO);

    /**
     * @brief Destructor
     */
    ~LotusLogger();

    /**
     * @brief Set minimum log level
     */
    void setLevel(LogLevel level);

    /**
     * @brief Check if logging is enabled for given level
     */
    bool isEnabled(LogLevel level) const;

    /**
     * @brief Log a message
     */
    void log(LogLevel level, const std::string& message);

    // Convenience methods
    void debug(const std::string& message) {
        log(LogLevel::DEBUG, message);
    }
    void info(const std::string& message) {
        log(LogLevel::INFO, message);
    }
    void warn(const std::string& message) {
        log(LogLevel::WARN, message);
    }
    void error(const std::string& message) {
        log(LogLevel::ERROR, message);
    }

  private:
    /**
     * @brief Rotate log files if current file exceeds max size
     */
    void rotateIfNeeded();

    /**
     * @brief Perform actual file rotation
     */
    void rotate();

    /**
     * @brief Get current timestamp string
     */
    std::string getTimestamp();

    /**
     * @brief Get log level string
     */
    std::string   levelToString(LogLevel level);

    std::string   log_file_;
    __off_t       max_size_;
    __off_t       max_files_;
    __off_t       current_size_;
    LogLevel      level_;
    std::ofstream file_;
    std::mutex    mutex_;
};

// Global logger instance
extern LotusLogger g_logger;

// Convenience macros
#define LOTUS_LOG_DEBUG(msg)                                                                                                                                                       \
    do {                                                                                                                                                                           \
        if (g_logger.isEnabled(LogLevel::DEBUG))                                                                                                                                   \
            g_logger.debug(msg);                                                                                                                                                   \
    } while (0)

#define LOTUS_LOG_INFO(msg)                                                                                                                                                        \
    do {                                                                                                                                                                           \
        if (g_logger.isEnabled(LogLevel::INFO))                                                                                                                                    \
            g_logger.info(msg);                                                                                                                                                    \
    } while (0)

#define LOTUS_LOG_WARN(msg)                                                                                                                                                        \
    do {                                                                                                                                                                           \
        if (g_logger.isEnabled(LogLevel::WARN))                                                                                                                                    \
            g_logger.warn(msg);                                                                                                                                                    \
    } while (0)

#define LOTUS_LOG_ERROR(msg)                                                                                                                                                       \
    do {                                                                                                                                                                           \
        if (g_logger.isEnabled(LogLevel::ERROR))                                                                                                                                   \
            g_logger.error(msg);                                                                                                                                                   \
    } while (0)

#endif // _LOTUS_LOGGER_H_