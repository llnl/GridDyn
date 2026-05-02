/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace spdlog {
class logger;
}

namespace helics {
/** class implementing a thread safe Logger
@details the Logger uses spdlog sinks to route formatted messages to the console and optional log
files.
*/
class Logger {
  private:
    std::atomic<bool> halted{true};  //!< indicator that the Logger was halted
    std::mutex loggerLock;  //!< mutex to protect logger state updates
    std::shared_ptr<spdlog::logger> consoleLogger;  //!< console sink used for log output
    std::shared_ptr<spdlog::logger> fileLogger;  //!< optional file sink used for log output
    std::string fileLoggerName;  //!< registry name for the file logger
    std::atomic<int> consoleLevel{100};  //!< level below which we need to print to the console
    std::atomic<int> fileLevel{100};  //!< level below which we need to print to a file
  public:
    /** default constructor*/
    Logger();
    /**destructor*/
    ~Logger();
    /** open a file to write the log messages
    @param file the name of the file to write messages to*/
    void openFile(const std::string& file);
    /** function to start the logging thread
    @param cLevel the console print level
    @param fLevel the file print level  messages coming in below these levels will be printed*/
    void startLogging(int cLevel, int fLevel);
    /** overload of @see startLogging with unspecified logging levels*/
    void startLogging() { startLogging(consoleLevel, fileLevel); }
    /** stop logging for a time messages received while halted are ignored*/
    void haltLogging();
    /** log a message at a particular level
    @param level the level of the message
    @param logMessage the actual message to log
    */
    void log(int level, std::string logMessage);
    /** message to log without regard for levels*
    @param logMessage the message to log
    */
    void log(std::string logMessage) { log(-100000, std::move(logMessage)); }
    /** flush the log queue*/
    void flush();
    /** check if the Logger is running*/
    bool isRunning() const;
    /** alter the printing levels
    @param cLevel the level to print to the console
    @param fLevel the level to print to the file if it is open*/
    void changeLevels(int cLevel, int fLevel);

  private:
    /** ensure a shared console logger exists*/
    void ensureConsoleLogger();
    /** release the file logger if one exists*/
    void resetFileLogger();
};

/** logging class that handle the logs immediately with no threading or synchronization*/
class LoggerNoThread {
  private:
    std::shared_ptr<spdlog::logger> consoleLogger;  //!< console sink used for log output
    std::shared_ptr<spdlog::logger> fileLogger;  //!< optional file sink used for log output
    std::string fileLoggerName;  //!< registry name for the file logger
  public:
    int consoleLevel = 100;  //!< level below which we need to print to the console
    int fileLevel = 100;  //!< level below which we need to print to a file
  public:
    /** default constructor*/
    LoggerNoThread();
    /** open a file to write the log messages
    @param file the name of the file to write messages to*/
    void openFile(const std::string& file);
    /** function to start the logging thread
    @param cLevel the console print level
    @param fLevel the file print level  messages coming in below these levels will be printed*/
    void startLogging(int cLevel, int fLevel);
    /** overload of /ref startLogging with unspecified logging levels*/
    void startLogging() { startLogging(consoleLevel, fileLevel); }
    // NOTE:: the interface for log in the noThreadLogging is slightly different
    // due to the threaded Logger making use of move semantics which isn't that useful in the
    // noThreadLogger
    /** log a message at a particular level
    @param level the level of the message
    @param logMessage the actual message to log
    */
    void log(int level, const std::string& logMessage);
    /** message to log without regard for levels*
    @param logMessage the message to log
    */
    void log(const std::string& logMessage) { log(-100000, logMessage); }
    /** check if the logging thread is running*/
    bool isRunning() const;
    /** flush the log queue*/
    void flush();
    /** alter the printing levels
    @param cLevel the level to print to the console
    @param fLevel the level to print to the file if it is open*/
    void changeLevels(int cLevel, int fLevel);

  private:
    /** ensure a shared console logger exists*/
    void ensureConsoleLogger();
    /** release the file logger if one exists*/
    void resetFileLogger();
};
}  // namespace helics
