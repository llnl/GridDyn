/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "logger.h"

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace {
static constexpr const char* griddynConsoleLoggerName{"griddyn_console"};
static std::atomic<unsigned int> loggerIdCounter{0U};

[[nodiscard]] std::string makeFileLoggerName()
{
    const auto loggerId = loggerIdCounter.fetch_add(1U, std::memory_order_relaxed);
    return "griddyn_file_" + std::to_string(loggerId);
}

[[nodiscard]] spdlog::level::level_enum getSpdlogLevel(int level)
{
    if (level >= 6) {
        return spdlog::level::trace;
    }
    if (level >= 5) {
        return spdlog::level::debug;
    }
    if (level >= 3) {
        return spdlog::level::info;
    }
    if (level >= 2) {
        return spdlog::level::warn;
    }
    if (level >= 1) {
        return spdlog::level::err;
    }
    return spdlog::level::critical;
}

void configureLogger(const std::shared_ptr<spdlog::logger>& logger)
{
    if (!logger) {
        return;
    }
    logger->set_pattern("%v");
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);
}
}  // namespace

namespace helics {
Logger::Logger()
{
    ensureConsoleLogger();
}

Logger::~Logger()
{
    std::lock_guard<std::mutex> lock(loggerLock);
    resetFileLogger();
}

void Logger::ensureConsoleLogger()
{
    if (consoleLogger) {
        return;
    }
    consoleLogger = spdlog::get(griddynConsoleLoggerName);
    if (!consoleLogger) {
        try {
            consoleLogger = spdlog::stdout_color_mt(griddynConsoleLoggerName);
        }
        catch (const spdlog::spdlog_ex&) {
            consoleLogger = spdlog::get(griddynConsoleLoggerName);
        }
    }
    configureLogger(consoleLogger);
}

void Logger::resetFileLogger()
{
    if (!fileLoggerName.empty()) {
        spdlog::drop(fileLoggerName);
        fileLoggerName.clear();
    }
    fileLogger.reset();
}

void Logger::openFile(const std::string& file)
{
    std::lock_guard<std::mutex> lock(loggerLock);
    resetFileLogger();
    fileLoggerName = makeFileLoggerName();
    fileLogger = spdlog::basic_logger_mt(fileLoggerName, file, true);
    configureLogger(fileLogger);
}

void Logger::startLogging(int cLevel, int fLevel)
{
    consoleLevel = cLevel;
    fileLevel = fLevel;
    halted.store(false);
}

void Logger::haltLogging()
{
    halted.store(true);
    flush();
}

void Logger::changeLevels(int cLevel, int fLevel)
{
    consoleLevel = cLevel;
    fileLevel = fLevel;
}

void Logger::log(int level, std::string logMessage)
{
    if (halted.load()) {
        return;
    }

    ensureConsoleLogger();
    const auto spdlogLevel = getSpdlogLevel(level);
    if (level <= consoleLevel.load() && consoleLogger) {
        consoleLogger->log(spdlogLevel, "{}", logMessage);
    }
    if (level <= fileLevel.load()) {
        std::lock_guard<std::mutex> lock(loggerLock);
        if (fileLogger) {
            fileLogger->log(spdlogLevel, "{}", logMessage);
        }
    }
}

void Logger::flush()
{
    ensureConsoleLogger();
    if (consoleLogger) {
        consoleLogger->flush();
    }
    std::lock_guard<std::mutex> lock(loggerLock);
    if (fileLogger) {
        fileLogger->flush();
    }
}

bool Logger::isRunning() const
{
    return !halted.load();
}

LoggerNoThread::LoggerNoThread()
{
    ensureConsoleLogger();
}

void LoggerNoThread::ensureConsoleLogger()
{
    if (consoleLogger) {
        return;
    }
    consoleLogger = spdlog::get(griddynConsoleLoggerName);
    if (!consoleLogger) {
        try {
            consoleLogger = spdlog::stdout_color_mt(griddynConsoleLoggerName);
        }
        catch (const spdlog::spdlog_ex&) {
            consoleLogger = spdlog::get(griddynConsoleLoggerName);
        }
    }
    configureLogger(consoleLogger);
}

void LoggerNoThread::resetFileLogger()
{
    if (!fileLoggerName.empty()) {
        spdlog::drop(fileLoggerName);
        fileLoggerName.clear();
    }
    fileLogger.reset();
}

void LoggerNoThread::openFile(const std::string& file)
{
    resetFileLogger();
    fileLoggerName = makeFileLoggerName();
    fileLogger = spdlog::basic_logger_mt(fileLoggerName, file, true);
    configureLogger(fileLogger);
}

void LoggerNoThread::startLogging(int cLevel, int fLevel)
{
    consoleLevel = cLevel;
    fileLevel = fLevel;
}

void LoggerNoThread::changeLevels(int cLevel, int fLevel)
{
    consoleLevel = cLevel;
    fileLevel = fLevel;
}

void LoggerNoThread::log(int level, const std::string& logMessage)
{
    ensureConsoleLogger();
    const auto spdlogLevel = getSpdlogLevel(level);
    if (level <= consoleLevel && consoleLogger) {
        consoleLogger->log(spdlogLevel, "{}", logMessage);
    }
    if (level <= fileLevel && fileLogger) {
        fileLogger->log(spdlogLevel, "{}", logMessage);
    }
}

void LoggerNoThread::flush()
{
    ensureConsoleLogger();
    if (consoleLogger) {
        consoleLogger->flush();
    }
    if (fileLogger) {
        fileLogger->flush();
    }
}

bool LoggerNoThread::isRunning() const
{
    return true;
}

}  // namespace helics
