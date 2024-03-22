#pragma once
#include <memory>
#include "coding.h"
#include "slice.h"

namespace LSMKV {

  class status {
  public:
      enum Code : uint8_t {
          kOk = 0,
          kNotFound = 1,
          kCorruption = 2,
          kNotSupported = 3,
          kInvalidArgument = 4,
          kIOError = 5,
      };

  public:
      status() noexcept = default;

      status(Code code) noexcept: code_(code) {
      }

      static status OK() noexcept {
          return {Code::kOk};
      }

      static status notFound() noexcept {
          return {Code::kNotFound};
      }

      static status IOError(Slice msg) noexcept {
          status s;
          s.code_ = Code::kIOError;
          s.state_ = std::make_unique<char[]>(msg.size() + 6);
          EncodeFixed32(s.state_.get(), static_cast<uint32_t>(msg.size()));
          s.state_[4] = static_cast<char>(s.code_);
          std::memcpy(s.state_.get() + 5, msg.data(), msg.size());
          s.state_[msg.size() + 5] = '\0';
          return s;
      }

      static status corruption() noexcept {
          return {Code::kCorruption};
      }

      static status notSupported() noexcept {
          return {Code::kNotSupported};
      }

      static status invalidArgument() noexcept {
          return {Code::kInvalidArgument};
      }

      [[nodiscard]] bool ok() const noexcept {
          return code_ == Code::kOk;
      }

      [[nodiscard]] bool is_not_found() const noexcept {
          return code_ == Code::kNotFound;
      }

      [[nodiscard]] bool is_corruption() const noexcept {
          return code_ == Code::kCorruption;
      }

      [[nodiscard]] bool is_not_supported() const noexcept {
          return code_ == Code::kNotSupported;
      }

      [[nodiscard]] bool is_invalid_argument() const noexcept {
          return code_ == Code::kInvalidArgument;
      }

      [[nodiscard]] bool isIOError() const noexcept {
          return code_ == Code::kIOError;
      }

  private:
      Code code_{};

      std::unique_ptr<char[]> state_;
  };


}// namespace LSMKV