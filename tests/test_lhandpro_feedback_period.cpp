// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_feedback_period.hpp"
#include "fakes/fake_lhandpro_sdk.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace roboparty::dexhand::detail;

namespace {

using SdoAccess = FakeLHandProSdk::SdoAccess;

constexpr std::array<unsigned int, 6> kOriginalPeriods{
    101U, 102U, 103U, 104U, 105U, 106U};

void seed_periods(FakeLHandProSdk& sdk,
                  const std::array<unsigned int, 6>& values =
                      kOriginalPeriods) {
  for (std::size_t axis = 0; axis < kFeedbackPeriodIndexes.size(); ++axis) {
    sdk.set_sdo_value(kFeedbackPeriodIndexes[axis], values[axis]);
  }
}

void check_accesses(const std::vector<SdoAccess>& actual,
                    std::size_t offset,
                    const std::array<unsigned int, 6>& values) {
  CHECK(actual.size() >= offset + values.size());
  for (std::size_t axis = 0; axis < values.size(); ++axis) {
    const auto& access = actual[offset + axis];
    CHECK_EQ(access.index, kFeedbackPeriodIndexes[axis]);
    CHECK_EQ(access.subindex, kFeedbackPeriodSubindex);
    CHECK_EQ(access.value, values[axis]);
  }
}

void append_accesses(std::vector<SdoAccess>& expected,
                     const std::array<unsigned int, 6>& values,
                     std::size_t count = kFeedbackPeriodIndexes.size()) {
  for (std::size_t axis = 0; axis < count; ++axis) {
    expected.push_back(
        {kFeedbackPeriodIndexes[axis], kFeedbackPeriodSubindex, values[axis]});
  }
}

void append_read_attempts(std::vector<SdoAccess>& expected,
                          std::size_t count =
                              kFeedbackPeriodIndexes.size()) {
  for (std::size_t axis = 0; axis < count; ++axis) {
    expected.push_back(
        {kFeedbackPeriodIndexes[axis], kFeedbackPeriodSubindex, 0U});
  }
}

void check_exact_accesses(const std::vector<SdoAccess>& actual,
                          const std::vector<SdoAccess>& expected) {
  CHECK_EQ(actual.size(), expected.size());
  for (std::size_t access = 0; access < expected.size(); ++access) {
    CHECK_EQ(actual[access].index, expected[access].index);
    CHECK_EQ(actual[access].subindex, expected[access].subindex);
    CHECK_EQ(actual[access].value, expected[access].value);
  }
}

void check_failure(const FeedbackPeriodReport& report,
                   FeedbackPeriodOutcome outcome, const char* operation,
                   int code, std::size_t axis) {
  CHECK_EQ(report.outcome, outcome);
  CHECK_EQ(report.failure.operation, std::string(operation));
  CHECK_EQ(report.failure.code, code);
  CHECK_EQ(report.failure.axis, axis);
}

void test_show_reads_all_axes_in_order() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.show();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::Shown);
  CHECK(report.success());
  CHECK_EQ(report.before, kOriginalPeriods);
  CHECK_EQ(report.after, kOriginalPeriods);
  CHECK_EQ(report.before_count, 6U);
  CHECK_EQ(report.after_count, 6U);
  check_accesses(sdk.sdo_read_snapshot(), 0, kOriginalPeriods);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 0U);
  CHECK_EQ(sdk.sdo_write_attempt_snapshot().size(), 0U);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
}

void test_show_reports_partial_read_failure() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("get_sdo_drive_param", 0);
  sdk.script_result("get_sdo_drive_param", 0);
  sdk.script_result("get_sdo_drive_param", 71);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.show();

  check_failure(report, FeedbackPeriodOutcome::ReadFailed,
                "get_sdo_drive_param", 71, 3);
  CHECK_EQ(report.before_count, 2U);
  CHECK_EQ(report.after_count, 0U);
  CHECK(!report.rollback_attempted);
  CHECK(!report.save_attempted);
  const auto reads = sdk.sdo_read_snapshot();
  CHECK_EQ(reads.size(), 2U);
  CHECK_EQ(reads[0].index, kFeedbackPeriodIndexes[0]);
  CHECK_EQ(reads[1].index, kFeedbackPeriodIndexes[1]);
  std::vector<SdoAccess> expected_attempts;
  append_read_attempts(expected_attempts, 3);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_attempts);
  CHECK_EQ(sdk.sdo_write_attempt_snapshot().size(), 0U);
}

void test_apply_returns_initial_read_failure_unchanged() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("get_sdo_drive_param", 0);
  sdk.script_result("get_sdo_drive_param", 0);
  sdk.script_result("get_sdo_drive_param", 72);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::ReadFailed,
                "get_sdo_drive_param", 72, 3);
  CHECK_EQ(report.before_count, 2U);
  CHECK_EQ(report.after_count, 0U);
  CHECK(!report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(!report.save_attempted);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads, 3);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  CHECK_EQ(sdk.sdo_write_attempt_snapshot().size(), 0U);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
}

void test_apply_skips_compliant_values() {
  FakeLHandProSdk sdk;
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::AlreadyCompliant);
  CHECK(report.success());
  CHECK_EQ(report.before_count, 6U);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(sdk.sdo_read_snapshot().size(), 6U);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 0U);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  CHECK_EQ(sdk.sdo_write_attempt_snapshot().size(), 0U);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  CHECK(!report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(!report.save_attempted);
}

void test_apply_mutates_verifies_and_saves() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.set_sdo_value(kFeedbackPeriodIndexes[0], 100U);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  const std::array<unsigned int, 6> expected_before{100U, 102U, 103U,
                                                      104U, 105U, 106U};
  const std::array<unsigned int, 6> target{200U, 200U, 200U,
                                            200U, 200U, 200U};
  CHECK_EQ(report.outcome, FeedbackPeriodOutcome::Saved);
  CHECK(report.success());
  CHECK_EQ(report.before, expected_before);
  CHECK_EQ(report.after, target);
  CHECK_EQ(report.before_count, 6U);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(sdk.sdo_read_snapshot().size(), 12U);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 6U);
  check_accesses(sdk.sdo_read_snapshot(), 0, expected_before);
  check_accesses(sdk.sdo_read_snapshot(), 6, target);
  check_accesses(sdk.sdo_write_snapshot(), 0, target);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes, target);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 1);
  CHECK(!report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(report.save_attempted);
}

void test_each_target_write_failure_rolls_back() {
  const std::array<unsigned int, 6> target{200U, 200U, 200U,
                                            200U, 200U, 200U};
  for (std::size_t failed_axis = 0; failed_axis < target.size(); ++failed_axis) {
    FakeLHandProSdk sdk;
    seed_periods(sdk);
    for (std::size_t axis = 0; axis < failed_axis; ++axis) {
      sdk.script_result("set_sdo_drive_param", 0);
    }
    sdk.script_result("set_sdo_drive_param", 80 + static_cast<int>(failed_axis));
    LHandProFeedbackPeriod transaction(sdk);

    const auto report = transaction.apply_20ms();

    check_failure(report, FeedbackPeriodOutcome::FailedRestored,
                  "set_sdo_drive_param", 80 + static_cast<int>(failed_axis),
                  failed_axis + 1);
    CHECK(report.rollback_attempted);
    CHECK(report.rollback_verified);
    CHECK(!report.save_attempted);
    CHECK_EQ(report.after_count, 6U);
    CHECK_EQ(report.after, kOriginalPeriods);
    CHECK_EQ(sdk.count("set_sdo_drive_param"),
             static_cast<int>(failed_axis + 1 + target.size()));
    CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
    const auto writes = sdk.sdo_write_snapshot();
    CHECK_EQ(writes.size(), failed_axis + target.size());
    for (std::size_t axis = 0; axis < failed_axis; ++axis) {
      CHECK_EQ(writes[axis].index, kFeedbackPeriodIndexes[axis]);
      CHECK_EQ(writes[axis].subindex, kFeedbackPeriodSubindex);
      CHECK_EQ(writes[axis].value, 200U);
    }
    check_accesses(writes, failed_axis, kOriginalPeriods);
    CHECK_EQ(sdk.sdo_read_snapshot().size(), 12U);
    check_accesses(sdk.sdo_read_snapshot(), 0, kOriginalPeriods);
    check_accesses(sdk.sdo_read_snapshot(), 6, kOriginalPeriods);
    std::vector<SdoAccess> expected_reads;
    append_read_attempts(expected_reads);
    append_read_attempts(expected_reads);
    check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
    std::vector<SdoAccess> expected_writes;
    append_accesses(expected_writes, target, failed_axis + 1);
    append_accesses(expected_writes, kOriginalPeriods);
    check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);

    const auto restored = transaction.show();
    CHECK_EQ(restored.outcome, FeedbackPeriodOutcome::Shown);
    CHECK_EQ(restored.before, kOriginalPeriods);
  }
}

void test_verification_read_failure_rolls_back() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  for (std::size_t read = 0; read < 8; ++read) {
    sdk.script_result("get_sdo_drive_param", 0);
  }
  sdk.script_result("get_sdo_drive_param", 91);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::FailedRestored,
                "get_sdo_drive_param", 91, 3);
  CHECK(report.rollback_attempted);
  CHECK(report.rollback_verified);
  CHECK(!report.save_attempted);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(report.after, kOriginalPeriods);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 12U);
  check_accesses(sdk.sdo_write_snapshot(), 0, std::array<unsigned int, 6>{
                                               200U, 200U, 200U,
                                               200U, 200U, 200U});
  check_accesses(sdk.sdo_write_snapshot(), 6, kOriginalPeriods);
  CHECK_EQ(sdk.count("get_sdo_drive_param"), 15);
  CHECK_EQ(sdk.sdo_read_snapshot().size(), 14U);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads, 3);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U});
  append_accesses(expected_writes, kOriginalPeriods);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_verification_mismatch_rolls_back() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  int reads = 0;
  sdk.before_call = [&](const std::string& operation) {
    if (operation == "get_sdo_drive_param" && ++reads == 7) {
      sdk.set_sdo_value(kFeedbackPeriodIndexes[2], 199U);
    }
  };
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::FailedRestored,
                "verify_feedback_period", -2, 0);
  CHECK(report.rollback_attempted);
  CHECK(report.rollback_verified);
  CHECK(!report.save_attempted);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(report.after, kOriginalPeriods);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  CHECK_EQ(sdk.sdo_read_snapshot().size(), 18U);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 12U);
  check_accesses(sdk.sdo_write_snapshot(), 0, std::array<unsigned int, 6>{
                                               200U, 200U, 200U,
                                               200U, 200U, 200U});
  check_accesses(sdk.sdo_write_snapshot(), 6, kOriginalPeriods);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U});
  append_accesses(expected_writes, kOriginalPeriods);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_failed_rollback_is_uncertain() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("set_sdo_drive_param", 97);
  sdk.script_result("set_sdo_drive_param", 98);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::FailedUncertain,
                "set_sdo_drive_param", 97, 1);
  CHECK(report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(!report.save_attempted);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(report.after, kOriginalPeriods);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  CHECK_EQ(sdk.count("set_sdo_drive_param"), 7);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U},
                  1);
  append_accesses(expected_writes, kOriginalPeriods);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_rollback_read_failure_clears_final_observation() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("set_sdo_drive_param", 121);
  for (int read = 0; read < 8; ++read) {
    sdk.script_result("get_sdo_drive_param", 0);
  }
  sdk.script_result("get_sdo_drive_param", 122);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::FailedUncertain,
                "set_sdo_drive_param", 121, 1);
  CHECK(report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(!report.save_attempted);
  CHECK_EQ(report.after_count, 0U);
  const std::array<unsigned int, 6> no_final_observation{};
  CHECK_EQ(report.after, no_final_observation);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  CHECK_EQ(sdk.count("get_sdo_drive_param"), 12);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U},
                  1);
  append_accesses(expected_writes, kOriginalPeriods);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_rollback_read_mismatch_reports_observed_snapshot() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("set_sdo_drive_param", 131);
  int reads = 0;
  sdk.before_call = [&](const std::string& operation) {
    if (operation == "get_sdo_drive_param" && ++reads == 7) {
      sdk.set_sdo_value(kFeedbackPeriodIndexes[3], 999U);
    }
  };
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  const std::array<unsigned int, 6> observed{101U, 102U, 103U,
                                               999U, 105U, 106U};
  check_failure(report, FeedbackPeriodOutcome::FailedUncertain,
                "set_sdo_drive_param", 131, 1);
  CHECK(report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(!report.save_attempted);
  CHECK_EQ(report.after_count, 6U);
  CHECK_EQ(report.after, observed);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 0);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U},
                  1);
  append_accesses(expected_writes, kOriginalPeriods);
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_save_failure_is_not_rolled_back() {
  FakeLHandProSdk sdk;
  seed_periods(sdk);
  sdk.script_result("save_sdo_drive_param", 111);
  LHandProFeedbackPeriod transaction(sdk);

  const auto report = transaction.apply_20ms();

  check_failure(report, FeedbackPeriodOutcome::SaveFailed,
                "save_sdo_drive_param", 111, 0);
  CHECK(!report.rollback_attempted);
  CHECK(!report.rollback_verified);
  CHECK(report.save_attempted);
  CHECK_EQ(sdk.count("save_sdo_drive_param"), 1);
  CHECK_EQ(sdk.sdo_write_snapshot().size(), 6U);
  std::vector<SdoAccess> expected_reads;
  append_read_attempts(expected_reads);
  append_read_attempts(expected_reads);
  check_exact_accesses(sdk.sdo_read_attempt_snapshot(), expected_reads);
  std::vector<SdoAccess> expected_writes;
  append_accesses(expected_writes,
                  std::array<unsigned int, 6>{200U, 200U, 200U,
                                               200U, 200U, 200U});
  check_exact_accesses(sdk.sdo_write_attempt_snapshot(), expected_writes);
}

void test_outcome_labels_and_success_predicate() {
  const std::array<std::pair<FeedbackPeriodOutcome, const char*>, 7> cases{{
      {FeedbackPeriodOutcome::Shown, "shown"},
      {FeedbackPeriodOutcome::AlreadyCompliant, "already-compliant"},
      {FeedbackPeriodOutcome::Saved, "saved"},
      {FeedbackPeriodOutcome::ReadFailed, "read-failed"},
      {FeedbackPeriodOutcome::FailedRestored, "failed-restored"},
      {FeedbackPeriodOutcome::FailedUncertain, "failed-uncertain"},
      {FeedbackPeriodOutcome::SaveFailed, "save-failed"},
  }};
  for (const auto& [outcome, label] : cases) {
    CHECK_EQ(std::string(feedback_period_outcome_name(outcome)),
             std::string(label));
    FeedbackPeriodReport report;
    report.outcome = outcome;
    CHECK_EQ(report.success(), outcome == FeedbackPeriodOutcome::Shown ||
                                    outcome == FeedbackPeriodOutcome::AlreadyCompliant ||
                                    outcome == FeedbackPeriodOutcome::Saved);
  }
  CHECK_EQ(std::string(feedback_period_outcome_name(
               static_cast<FeedbackPeriodOutcome>(999))),
           std::string("unknown"));
}

}  // namespace

int main() {
  test_show_reads_all_axes_in_order();
  test_show_reports_partial_read_failure();
  test_apply_returns_initial_read_failure_unchanged();
  test_apply_skips_compliant_values();
  test_apply_mutates_verifies_and_saves();
  test_each_target_write_failure_rolls_back();
  test_verification_read_failure_rolls_back();
  test_verification_mismatch_rolls_back();
  test_failed_rollback_is_uncertain();
  test_rollback_read_failure_clears_final_observation();
  test_rollback_read_mismatch_reports_observed_snapshot();
  test_save_failure_is_not_rolled_back();
  test_outcome_labels_and_success_predicate();
  return 0;
}
