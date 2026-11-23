// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>

/**
 * @class Logger
 * @brief Provides simple logging utilities.
 */
class Logger
{
 public:
  /**
   * @brief Initializes the logging system. The previous session's log is
   * kept as RuntimePaths::previousLogPath().
   */
  static void init();
  /** Closes the log and releases its file handle. */
  static void shutdown();

  /**
   * @brief Logs an informational message.
   * @param msg The message to log.
   */
  static void info(const std::string& msg);

  /**
   * @brief Logs an error message.
   * @param msg The message to log.
   */
  static void error(const std::string& msg);

  /**
   * @brief Logs a debug message.
   * @param msg The message to log.
   */
  static void debug(const std::string& msg);

  /**
   * @brief Logs a warning message.
   * @param msg The message to log.
   */
  static void warn(const std::string& msg);

  /** Writes buffered messages to the log file now. */
  static void flush();
};
