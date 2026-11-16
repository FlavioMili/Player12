// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <system_error>

#include "global/paths.h"
#include "global/runtime_paths.h"

namespace
{
std::shared_ptr<spdlog::logger> logger;

spdlog::logger& getLogger()
{
  if (!logger) Logger::init();
  return *logger;
}
}  // namespace

void Logger::init()
{
  if (!logger)
  {
    // The log of the session before (maybe one that crashed) is kept once.
    const std::filesystem::path log = RuntimePaths::logPath();
    if (std::error_code error;
        std::filesystem::file_size(log, error) > 0 && !error)
      std::filesystem::rename(log, RuntimePaths::previousLogPath(), error);

    std::shared_ptr<spdlog::sinks::sink> file_sink =
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            RuntimePaths::logPath().string(), true);
    std::shared_ptr<spdlog::sinks::sink> console_sink =
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    logger = std::make_shared<spdlog::logger>(
        "main_logger", spdlog::sinks_init_list{console_sink, file_sink});

    logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
    logger->set_level(spdlog::level::debug);
    // Warnings and errors reach the file at once, even if the game crashes.
    logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(logger);
  }
}

void Logger::info(const std::string& msg) { getLogger().info(msg); }
void Logger::error(const std::string& msg) { getLogger().error(msg); }
void Logger::warn(const std::string& msg) { getLogger().warn(msg); }
void Logger::flush() { getLogger().flush(); }

#ifdef DEBUG
void Logger::debug(const std::string& msg) { getLogger().debug(msg); }
#else
void Logger::debug(const std::string& msg) { (void)msg; }
#endif
