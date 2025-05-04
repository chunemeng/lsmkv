#pragma once

#include <source_location>

#include "spdlog/spdlog.h"
#include "spdlog/async_logger.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/async.h"
#include "slice.h"

namespace LSMKV {
  static inline std::string line_info(std::source_location s = std::source_location::current()) {
      return std::string(s.file_name()) + ":" + std::to_string(s.line());
  }
} // namespace LSMKV

namespace LSMKV::log {

  class LoggerSingleton {
  public:
      LoggerSingleton(const LoggerSingleton &) = delete;

      LoggerSingleton &operator=(const LoggerSingleton &) = delete;

      static spdlog::async_logger &instance() {
          static LoggerSingleton s_instance{};
          return *s_instance.logger_;
      }

  private:
      LoggerSingleton() {
          thread_pool_ = std::make_shared<spdlog::details::thread_pool>(8192, 1);
          auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
          logger_ = std::make_shared<spdlog::async_logger>(
                  "LSMKV",
                  sink,
                  thread_pool_,
                  spdlog::async_overflow_policy::block
          );
          spdlog::register_logger(logger_);
      }

      ~LoggerSingleton() {
          spdlog::drop("LSMKV");
      }

      std::shared_ptr<spdlog::details::thread_pool> thread_pool_;
      std::shared_ptr<spdlog::async_logger> logger_;
  };

  inline spdlog::async_logger &logger() {
      return LoggerSingleton::instance();
  }

  static inline void info(const std::string &msg) {
      logger().info(msg);
  }

  static inline void debug(const std::string &msg) {
      logger().debug(msg);
  }

  static inline void warn(const std::string &msg) {
      logger().warn(msg);
  }

  static inline void error(const std::string &msg) {
      logger().error(msg);
  }

  template<typename... Args>
  static inline void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
      logger().debug(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static inline void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
      logger().info(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static inline void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
      logger().warn(fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static inline void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
      logger().error(fmt, std::forward<Args>(args)...);
  }

  static inline std::string current_info(Slice msg) {
      std::source_location loc = std::source_location::current();
      return fmt::format("{}:{}:{}", loc.file_name(), loc.line(), loc.function_name()) + " " + msg.toString();
  }


} // namespace LSMKV