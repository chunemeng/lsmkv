#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/async_logger.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/async.h"
#include <source_location>

namespace LSMKV::log {

  static inline spdlog::async_logger logger() {
      static auto logger = spdlog::async_logger("LSMKV", spdlog::sinks_init_list{
                                                        std::make_shared<spdlog::sinks::stdout_sink_mt>()},
                                                std::make_shared<spdlog::details::thread_pool>(8192, 1),
                                                spdlog::async_overflow_policy::block);
      return logger;
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