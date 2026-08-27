// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace roboparty::dexhand::detail {

inline constexpr std::array<unsigned int, 6> kFeedbackPeriodIndexes{
    0x201D, 0x205D, 0x209D, 0x20DD, 0x211D, 0x215D};
inline constexpr unsigned char kFeedbackPeriodSubindex = 0x14;
inline constexpr unsigned int kFeedbackPeriod20msUnits = 200;

enum class FeedbackPeriodOutcome {
  Shown,
  AlreadyCompliant,
  Saved,
  ReadFailed,
  FailedRestored,
  FailedUncertain,
  SaveFailed,
};

struct FeedbackPeriodFailure {
  std::string operation;
  int code{0};
  // 1-based when an axis is implicated, otherwise 0.
  std::size_t axis{0};
};

struct FeedbackPeriodReport {
  FeedbackPeriodOutcome outcome{FeedbackPeriodOutcome::ReadFailed};
  std::array<unsigned int, 6> before{};
  std::array<unsigned int, 6> after{};
  std::size_t before_count{0};
  std::size_t after_count{0};
  FeedbackPeriodFailure failure{};
  bool rollback_attempted{false};
  bool rollback_verified{false};
  bool save_attempted{false};

  bool success() const noexcept;
};

class LHandProFeedbackPeriod final {
 public:
  using VerifiedSet = std::function<int(unsigned int, unsigned char,
                                        unsigned int)>;
  using VerifiedSave = std::function<int()>;

  explicit LHandProFeedbackPeriod(
      LHandProSdk&,
      std::function<bool()> continue_allowed = {},
      VerifiedSet verified_set = {}, VerifiedSave verified_save = {}) noexcept;

  FeedbackPeriodReport show();
  FeedbackPeriodReport apply_20ms();

 private:
  bool read_all_(std::array<unsigned int, 6>& values, std::size_t& count,
                 FeedbackPeriodFailure& failure);
  bool write_all_(const std::array<unsigned int, 6>& values,
                  FeedbackPeriodFailure& failure);
  bool rollback_(const std::array<unsigned int, 6>& before,
                 FeedbackPeriodReport& report);
  bool continuation_allowed_() noexcept;

  LHandProSdk& sdk_;
  std::function<bool()> continue_allowed_;
  VerifiedSet verified_set_;
  VerifiedSave verified_save_;
};

const char* feedback_period_outcome_name(FeedbackPeriodOutcome) noexcept;

}  // namespace roboparty::dexhand::detail
