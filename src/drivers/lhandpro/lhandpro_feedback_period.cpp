// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_feedback_period.hpp"

#include <utility>

namespace roboparty::dexhand::detail {

bool FeedbackPeriodReport::success() const noexcept {
  return outcome == FeedbackPeriodOutcome::Shown ||
         outcome == FeedbackPeriodOutcome::AlreadyCompliant ||
         outcome == FeedbackPeriodOutcome::Saved;
}

LHandProFeedbackPeriod::LHandProFeedbackPeriod(
    LHandProSdk& sdk, std::function<bool()> continue_allowed) noexcept
    : sdk_(sdk), continue_allowed_(std::move(continue_allowed)) {}

FeedbackPeriodReport LHandProFeedbackPeriod::show() {
  FeedbackPeriodReport report;
  if (!read_all_(report.before, report.before_count, report.failure)) {
    return report;
  }

  report.after = report.before;
  report.after_count = report.before_count;
  report.outcome = FeedbackPeriodOutcome::Shown;
  return report;
}

FeedbackPeriodReport LHandProFeedbackPeriod::apply_20ms() {
  FeedbackPeriodReport report = show();
  if (report.outcome == FeedbackPeriodOutcome::ReadFailed) return report;
  if (!continuation_allowed_()) {
    report.failure = {"transaction_cancelled", -3, 0};
    report.outcome = FeedbackPeriodOutcome::ReadFailed;
    return report;
  }

  const std::array<unsigned int, 6> target{
      kFeedbackPeriod20msUnits, kFeedbackPeriod20msUnits,
      kFeedbackPeriod20msUnits, kFeedbackPeriod20msUnits,
      kFeedbackPeriod20msUnits, kFeedbackPeriod20msUnits};
  if (report.before == target) {
    report.outcome = FeedbackPeriodOutcome::AlreadyCompliant;
    return report;
  }

  if (!write_all_(target, report.failure)) {
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }
  if (!continuation_allowed_()) {
    report.failure = {"transaction_cancelled", -3, 0};
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }

  report.after_count = 0;
  if (!read_all_(report.after, report.after_count, report.failure)) {
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }
  if (report.after != target) {
    report.failure = {"verify_feedback_period", -2, 0};
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }
  if (!continuation_allowed_()) {
    report.failure = {"transaction_cancelled", -3, 0};
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }

  report.save_attempted = true;
  const int save_code = sdk_.save_sdo_drive_param();
  if (save_code != 0) {
    report.failure = {"save_sdo_drive_param", save_code, 0};
    report.outcome = FeedbackPeriodOutcome::SaveFailed;
    return report;
  }

  report.outcome = FeedbackPeriodOutcome::Saved;
  return report;
}

bool LHandProFeedbackPeriod::read_all_(std::array<unsigned int, 6>& values,
                                       std::size_t& count,
                                       FeedbackPeriodFailure& failure) {
  count = 0;
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    const int code = sdk_.get_sdo_drive_param(
        kFeedbackPeriodIndexes[axis], kFeedbackPeriodSubindex, values[axis]);
    if (code != 0) {
      failure = {"get_sdo_drive_param", code, axis + 1};
      return false;
    }
    ++count;
  }
  return true;
}

bool LHandProFeedbackPeriod::write_all_(
    const std::array<unsigned int, 6>& values, FeedbackPeriodFailure& failure) {
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    const int code = sdk_.set_sdo_drive_param(
        kFeedbackPeriodIndexes[axis], kFeedbackPeriodSubindex, values[axis]);
    if (code != 0) {
      failure = {"set_sdo_drive_param", code, axis + 1};
      return false;
    }
  }
  return true;
}

bool LHandProFeedbackPeriod::rollback_(
    const std::array<unsigned int, 6>& before, FeedbackPeriodReport& report) {
  report.rollback_attempted = true;
  bool writes_succeeded = true;
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    if (sdk_.set_sdo_drive_param(kFeedbackPeriodIndexes[axis],
                                 kFeedbackPeriodSubindex, before[axis]) != 0) {
      writes_succeeded = false;
    }
  }

  std::array<unsigned int, 6> restored{};
  bool reads_succeeded = true;
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    if (sdk_.get_sdo_drive_param(kFeedbackPeriodIndexes[axis],
                                 kFeedbackPeriodSubindex, restored[axis]) !=
        0) {
      reads_succeeded = false;
    }
  }
  if (reads_succeeded) {
    report.after = restored;
    report.after_count = restored.size();
  } else {
    report.after = {};
    report.after_count = 0;
  }
  report.rollback_verified =
      writes_succeeded && reads_succeeded && restored == before;
  return report.rollback_verified;
}

bool LHandProFeedbackPeriod::continuation_allowed_() noexcept {
  if (!continue_allowed_) return true;
  try {
    return continue_allowed_();
  } catch (...) {
    return false;
  }
}

const char* feedback_period_outcome_name(FeedbackPeriodOutcome outcome) noexcept {
  switch (outcome) {
    case FeedbackPeriodOutcome::Shown:
      return "shown";
    case FeedbackPeriodOutcome::AlreadyCompliant:
      return "already-compliant";
    case FeedbackPeriodOutcome::Saved:
      return "saved";
    case FeedbackPeriodOutcome::ReadFailed:
      return "read-failed";
    case FeedbackPeriodOutcome::FailedRestored:
      return "failed-restored";
    case FeedbackPeriodOutcome::FailedUncertain:
      return "failed-uncertain";
    case FeedbackPeriodOutcome::SaveFailed:
      return "save-failed";
  }
  return "unknown";
}

}  // namespace roboparty::dexhand::detail
