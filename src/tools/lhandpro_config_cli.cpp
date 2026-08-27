// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "tools/lhandpro_config_cli.hpp"

#include "drivers/lhandpro/lhandpro_driver.hpp"
#include "drivers/lhandpro/lhandpro_feedback_period.hpp"

#include <charconv>
#include <exception>
#include <iomanip>
#include <ostream>
#include <string>
#include <system_error>

namespace roboparty::dexhand::detail {
namespace {

enum class Command { Show, Apply };

struct Arguments {
  Command command{Command::Show};
  std::string interface;
  int node_id{0};
};

void print_usage(std::ostream& stream) {
  stream
      << "Usage:\n"
      << "  roboparty-dexhand-config feedback-period show\n"
      << "    --interface NAME --node-id ID\n"
      << "  roboparty-dexhand-config feedback-period apply\n"
      << "    --interface NAME --node-id ID --milliseconds 20 --save\n";
}

bool parse_integer(const std::string& text, int& value) {
  if (text.empty()) return false;
  const char* const first = text.data();
  const char* const last = first + text.size();
  const auto parsed = std::from_chars(first, last, value);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}

bool parse_arguments(int argc, const char* const argv[], Arguments& parsed,
                     std::string& diagnostic) {
  if (argc < 3 || argv == nullptr || argv[1] == nullptr ||
      argv[2] == nullptr) {
    diagnostic = "expected feedback-period show or apply";
    return false;
  }
  if (std::string(argv[1]) != "feedback-period") {
    diagnostic = "expected feedback-period command";
    return false;
  }
  const std::string action(argv[2]);
  if (action == "show") {
    parsed.command = Command::Show;
  } else if (action == "apply") {
    parsed.command = Command::Apply;
  } else {
    diagnostic = "expected show or apply";
    return false;
  }

  bool has_interface = false;
  bool has_node_id = false;
  bool has_milliseconds = false;
  bool has_save = false;
  int milliseconds = 0;

  for (int index = 3; index < argc; ++index) {
    if (argv[index] == nullptr) {
      diagnostic = "invalid null argument";
      return false;
    }
    const std::string option(argv[index]);
    if (option == "--save") {
      if (has_save) {
        diagnostic = "duplicate --save";
        return false;
      }
      has_save = true;
      continue;
    }

    if (option != "--interface" && option != "--node-id" &&
        option != "--milliseconds") {
      diagnostic = "unknown option or positional argument: " + option;
      return false;
    }
    if (index + 1 >= argc || argv[index + 1] == nullptr) {
      diagnostic = "missing value for " + option;
      return false;
    }
    const std::string value(argv[++index]);
    if (option == "--interface") {
      if (has_interface) {
        diagnostic = "duplicate --interface";
        return false;
      }
      has_interface = true;
      parsed.interface = value;
    } else if (option == "--node-id") {
      if (has_node_id) {
        diagnostic = "duplicate --node-id";
        return false;
      }
      has_node_id = true;
      if (!parse_integer(value, parsed.node_id)) {
        diagnostic = "invalid --node-id";
        return false;
      }
    } else {
      if (has_milliseconds) {
        diagnostic = "duplicate --milliseconds";
        return false;
      }
      has_milliseconds = true;
      if (!parse_integer(value, milliseconds)) {
        diagnostic = "invalid --milliseconds";
        return false;
      }
    }
  }

  if (!has_interface || parsed.interface.empty()) {
    diagnostic = "--interface is required and must be nonempty";
    return false;
  }
  if (!has_node_id || parsed.node_id < 1 || parsed.node_id > 127) {
    diagnostic = "--node-id must be in the range 1..127";
    return false;
  }
  if (parsed.command == Command::Show) {
    if (has_milliseconds || has_save) {
      diagnostic = "show does not accept --milliseconds or --save";
      return false;
    }
    return true;
  }
  if (!has_milliseconds || milliseconds != 20) {
    diagnostic = "apply requires --milliseconds 20";
    return false;
  }
  if (!has_save) {
    diagnostic = "apply requires explicit --save";
    return false;
  }
  return true;
}

void print_report(const FeedbackPeriodReport& report, Command command,
                  std::ostream& output) {
  output << "result=" << feedback_period_outcome_name(report.outcome) << '\n';
  for (std::size_t axis = 0; axis < report.before_count; ++axis) {
    output << "axis=" << (axis + 1) << " index=0x" << std::uppercase
           << std::hex << std::setw(4) << std::setfill('0')
           << kFeedbackPeriodIndexes[axis] << std::dec << std::nouppercase
           << std::setfill(' ') << " before=" << report.before[axis];
    if (axis < report.after_count) output << " after=" << report.after[axis];
    output << '\n';
  }

  if (!report.success()) {
    output << "failure-operation=" << report.failure.operation
           << " failure-axis=" << report.failure.axis
           << " failure-code=" << report.failure.code << '\n'
           << "rollback-attempted=" << (report.rollback_attempted ? 1 : 0)
           << '\n'
           << "rollback-verified=" << (report.rollback_verified ? 1 : 0)
           << '\n'
           << "save-attempted=" << (report.save_attempted ? 1 : 0) << '\n';
    return;
  }

  if (command == Command::Apply) {
    output << "save-attempted=" << (report.save_attempted ? 1 : 0) << '\n';
    if (report.outcome == FeedbackPeriodOutcome::Saved) {
      output << "power-cycle the hand, then run feedback-period show to "
                "confirm persistence\n";
    }
  }
}

std::string exception_message(const char* prefix,
                              const std::exception* exception) {
  std::string message(prefix);
  if (exception != nullptr) message += std::string(": ") + exception->what();
  return message;
}

}  // namespace

int finish_lhandpro_config_cli(
    int result, const std::string& primary_diagnostic, std::ostream& error,
    const std::function<void()>& cleanup) {
  std::string cleanup_diagnostic;
  try {
    cleanup();
  } catch (const std::exception& exception) {
    cleanup_diagnostic = exception_message("cleanup failed", &exception);
  } catch (...) {
    cleanup_diagnostic = exception_message("cleanup failed", nullptr);
  }
  if (!cleanup_diagnostic.empty()) result = 1;
  if (!primary_diagnostic.empty()) error << primary_diagnostic << '\n';
  if (!cleanup_diagnostic.empty()) error << cleanup_diagnostic << '\n';
  return result;
}

int run_lhandpro_config_cli(int argc, const char* const argv[],
                            std::ostream& output, std::ostream& error,
                            const ConfigDriverFactory& factory) {
  for (int index = 1; index < argc; ++index) {
    if (argv != nullptr && argv[index] != nullptr &&
        std::string(argv[index]) == "--help") {
      print_usage(output);
      return 0;
    }
  }

  Arguments arguments;
  std::string diagnostic;
  if (!parse_arguments(argc, argv, arguments, diagnostic)) {
    error << "usage error: " << diagnostic << '\n';
    print_usage(error);
    return 2;
  }

  output << "interface=" << arguments.interface
         << " node-id=" << arguments.node_id << '\n';
  if (!factory) {
    error << "runtime error: factory unavailable\n";
    return 1;
  }

  std::unique_ptr<LHandProDriver> driver;
  try {
    driver = factory(arguments.interface, arguments.node_id);
  } catch (const std::exception& exception) {
    error << exception_message("factory failed", &exception) << '\n';
    return 1;
  } catch (...) {
    error << exception_message("factory failed", nullptr) << '\n';
    return 1;
  }
  if (!driver) {
    error << "runtime error: factory returned null\n";
    return 1;
  }

  int result = 1;
  std::string primary_diagnostic;
  try {
    if (!driver->init_for_provisioning()) {
      primary_diagnostic = "initialization failed";
    } else {
      const auto report = arguments.command == Command::Show
                              ? driver->show_feedback_period()
                              : driver->apply_feedback_period_20ms();
      print_report(report, arguments.command, output);
      result = report.success() ? 0 : 1;
      if (!report.success()) {
        primary_diagnostic = "feedback-period failed";
        if (report.outcome == FeedbackPeriodOutcome::FailedUncertain) {
          primary_diagnostic +=
              "\nwarning: current transient device state is uncertain; "
              "power-cycle before continuing";
        } else if (report.outcome == FeedbackPeriodOutcome::SaveFailed) {
          primary_diagnostic +=
              "\nwarning: running feedback-period values are 200; "
              "persistence is unknown";
        }
      }
    }
  } catch (const std::exception& exception) {
    primary_diagnostic = exception_message("operation failed", &exception);
  } catch (...) {
    primary_diagnostic = exception_message("operation failed", nullptr);
  }

  return finish_lhandpro_config_cli(
      result, primary_diagnostic, error, [&] { driver->deinit_hand(); });
}

std::unique_ptr<LHandProDriver> make_lhandpro_config_driver(
    const std::string& interface, int node_id) {
  return std::make_unique<LHandProDriver>(interface, LHandProModel::Dof6S,
                                          node_id);
}

}  // namespace roboparty::dexhand::detail
