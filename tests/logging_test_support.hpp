// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

class RecordingLogSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  int count() const noexcept { return count_.load(std::memory_order_relaxed); }

  std::string messages() const {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    return messages_;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    messages_.clear();
    count_.store(0, std::memory_order_relaxed);
  }

  void set_throw_on_log(bool enabled) noexcept {
    throw_on_log_.store(enabled, std::memory_order_relaxed);
  }

 private:
  void sink_it_(const spdlog::details::log_msg& message) override {
    {
      std::lock_guard<std::mutex> lock(messages_mutex_);
      messages_.append(message.payload.data(), message.payload.size());
      messages_.push_back('\n');
    }
    count_.fetch_add(1, std::memory_order_relaxed);
    if (throw_on_log_.load(std::memory_order_relaxed)) {
      throw std::runtime_error("test log sink failure");
    }
  }

  void flush_() override {}

  std::atomic<int> count_{0};
  std::atomic<bool> throw_on_log_{false};
  mutable std::mutex messages_mutex_;
  std::string messages_;
};

class ScopedTestLoggers final {
 public:
  ScopedTestLoggers()
      : original_default_(spdlog::default_logger()),
        default_sink_(std::make_shared<RecordingLogSink>()),
        named_sink_(std::make_shared<RecordingLogSink>()),
        default_logger_(std::make_shared<spdlog::logger>(
            "dexhand-test-default", default_sink_)),
        named_logger_(
            std::make_shared<spdlog::logger>("dexhand", named_sink_)) {
    spdlog::drop("dexhand");
    spdlog::set_default_logger(default_logger_);
    spdlog::register_logger(named_logger_);
  }

  ~ScopedTestLoggers() {
    try {
      spdlog::drop("dexhand");
      spdlog::set_default_logger(original_default_);
    } catch (...) {
    }
  }

  void drop_named() { spdlog::drop("dexhand"); }

  void register_named() {
    if (!spdlog::get("dexhand")) spdlog::register_logger(named_logger_);
  }

  const std::shared_ptr<RecordingLogSink>& default_sink() const noexcept {
    return default_sink_;
  }

  const std::shared_ptr<RecordingLogSink>& named_sink() const noexcept {
    return named_sink_;
  }

  const std::shared_ptr<spdlog::logger>& named_logger() const noexcept {
    return named_logger_;
  }

 private:
  std::shared_ptr<spdlog::logger> original_default_;
  std::shared_ptr<RecordingLogSink> default_sink_;
  std::shared_ptr<RecordingLogSink> named_sink_;
  std::shared_ptr<spdlog::logger> default_logger_;
  std::shared_ptr<spdlog::logger> named_logger_;
};
