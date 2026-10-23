// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "global/crash_report.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#endif

#include "global/build_info.h"
#include "global/logger.h"
#include "global/runtime_paths.h"

namespace fs = std::filesystem;

namespace
{
using PathChar = fs::path::value_type;
constexpr std::size_t PATH_CAPACITY = 4096;
constexpr std::size_t HEADER_CAPACITY = 2048;
constexpr std::size_t NAME_CAPACITY = 64;
constexpr std::size_t KEPT_REPORTS = 10;
constexpr int BACKTRACE_FRAMES = 64;
constexpr const char* PENDING_FILE = "crash.pending";
constexpr std::string_view REPORT_PREFIX = "crash-";

// Everything a signal handler needs is prepared up front: in the handler
// only async-signal-safe calls are made (open, write, close, signal, raise).
struct CrashState
{
  std::array<PathChar, PATH_CAPACITY> report_path{};
  std::array<PathChar, PATH_CAPACITY> pending_path{};
  std::array<char, NAME_CAPACITY> report_name{};
  std::size_t report_name_length = 0;
  std::array<char, HEADER_CAPACITY> header{};
  std::size_t header_length = 0;
  std::atomic<bool> prepared{false};
  std::atomic_flag written{};
};

CrashState& crashState()
{
  // Constant initialization avoids a lazy initialization guard in the handler.
  constinit static CrashState state;
  return state;
}

bool copyPath(const fs::path& path, std::array<PathChar, PATH_CAPACITY>& target)
{
  const fs::path::string_type& native = path.native();
  if (native.size() >= PATH_CAPACITY) return false;
  std::ranges::copy(native, target.begin());
  target[native.size()] = PathChar{};
  return true;
}

/** Report name and header for this session (stamped with its start time). */
void prepare()
{
  auto& state = crashState();
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  const std::string stamp = std::format("{:%Y%m%dT%H%M%SZ}", now);
  const std::string name = std::string(REPORT_PREFIX) + stamp + ".txt";
  const fs::path folder = CrashReport::directory();
  const std::size_t name_length = std::min(name.size(), NAME_CAPACITY - 1);
  std::memcpy(state.report_name.data(), name.data(), name_length);
  state.report_name[name_length] = '\0';
  state.report_name_length = name_length;
  if (!copyPath(folder / name, state.report_path))
    state.report_path[0] = PathChar{};
  if (!copyPath(folder / PENDING_FILE, state.pending_path))
    state.pending_path[0] = PathChar{};

  const std::string text = std::format(
      "Player12 crash report\n"
      "Version: {}\n"
      "Build: commit {}, {}, {}, {}\n"
      "Session started: {:%FT%TZ}\n"
      "Session log: crash-{}.log next to this file (kept at the next start)\n"
      "This report stays on this computer; nothing was sent anywhere.\n\n",
      BuildInfo::version(), BuildInfo::commit(), BuildInfo::configuration(),
      BuildInfo::compiler(), BuildInfo::platform(), now, stamp);
  state.header_length = std::min(text.size(), HEADER_CAPACITY - 1);
  std::memcpy(state.header.data(), text.data(), state.header_length);
  state.prepared.store(true);
}

int openForWrite(const PathChar* path)
{
  if (path[0] == PathChar{}) return -1;
#if defined(_WIN32)
  int fd = -1;
  if (_wsopen_s(&fd, path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0)
    return -1;
  return fd;
#else
  return ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
}

void writeAll(int fd, const char* data, std::size_t size)
{
  while (size > 0)
  {
#if defined(_WIN32)
    const int count = _write(fd, data, static_cast<unsigned>(size));
#else
    const ssize_t count = ::write(fd, data, size);
#endif
    if (count <= 0) return;
    data += count;
    size -= static_cast<std::size_t>(count);
  }
}

template <std::size_t Size>
void writeText(int fd, const char (&text)[Size])
{
  writeAll(fd, text, Size - 1);
}

void closeFile(int fd)
{
#if defined(_WIN32)
  _close(fd);
#else
  ::close(fd);
#endif
}

/** Signal-safe: writes the report and the pending marker. */
void writeReport(const char* reason, std::size_t reason_length)
{
  auto& state = crashState();
  if (const int fd = openForWrite(state.report_path.data()); fd >= 0)
  {
    writeAll(fd, state.header.data(), state.header_length);
    writeText(fd, "Reason: ");
    writeAll(fd, reason, reason_length);
    writeText(fd, "\n");
#if __has_include(<execinfo.h>)
    std::array<void*, BACKTRACE_FRAMES> frames{};
    const int count = backtrace(frames.data(), BACKTRACE_FRAMES);
    writeText(fd, "\nBacktrace:\n");
    backtrace_symbols_fd(frames.data(), count, fd);
#endif
    closeFile(fd);
  }
  if (const int fd = openForWrite(state.pending_path.data()); fd >= 0)
  {
    writeAll(fd, state.report_name.data(), state.report_name_length);
    closeFile(fd);
  }
}

std::string_view signalReason(int signal_number)
{
  switch (signal_number)
  {
    case SIGSEGV:
      return "fatal signal SIGSEGV (invalid memory access)";
    case SIGILL:
      return "fatal signal SIGILL (illegal instruction)";
    case SIGFPE:
      return "fatal signal SIGFPE (arithmetic error)";
    case SIGABRT:
      return "fatal signal SIGABRT (aborted)";
#if defined(SIGBUS)
    case SIGBUS:
      return "fatal signal SIGBUS (bus error)";
#endif
    default:
      return "fatal signal";
  }
}

void onFatalSignal(int signal_number)
{
  auto& state = crashState();
  if (!state.written.test_and_set())
  {
    const std::string_view reason = signalReason(signal_number);
    writeReport(reason.data(), reason.size());
  }
  // Default action from here: the process still ends with this signal.
  std::signal(signal_number, SIG_DFL);
  std::raise(signal_number);
}

[[noreturn]] void onTerminate()
{
  std::string reason = "std::terminate without an active exception";
  if (const std::exception_ptr error = std::current_exception())
  {
    try
    {
      std::rethrow_exception(error);
    }
    catch (const std::exception& exception)
    {
      reason = std::string("unhandled exception: ") + exception.what();
    }
    catch (...)
    {
      reason = "unhandled exception of an unknown type";
    }
  }
  CrashReport::write(reason);
  std::abort();
}

fs::path withLogExtension(fs::path report)
{
  report.replace_extension(".log");
  return report;
}

/** Keeps the log of the session that crashed next to its report. */
void keepCrashedSessionLog()
{
  const std::optional<CrashReport::Pending> crash = CrashReport::pending();
  if (!crash || !crash->log.empty()) return;
  std::error_code error;
  const fs::path previous = RuntimePaths::previousLogPath();
  if (fs::is_regular_file(previous, error))
    fs::copy_file(previous, withLogExtension(crash->report),
                  fs::copy_options::skip_existing, error);
}
}  // namespace

namespace CrashReport
{
fs::path directory() { return RuntimePaths::logPath().parent_path(); }

void install()
{
  auto& state = crashState();
  prepare();
  state.written.clear();
  keepCrashedSessionLog();
#if __has_include(<execinfo.h>)
  // The first backtrace() loads the unwinder; do it now, not in a handler.
  void* frame = nullptr;
  backtrace(&frame, 1);
#endif
  std::set_terminate(onTerminate);
#if defined(_WIN32)
  for (const int signal_number : {SIGSEGV, SIGILL, SIGFPE, SIGABRT})
    std::signal(signal_number, onFatalSignal);
#else
  // A separate stack lets the handler run after a stack overflow.
  static std::array<char, 64 * 1024> alternate_stack{};
  stack_t stack{};
  stack.ss_sp = alternate_stack.data();
  stack.ss_size = alternate_stack.size();
  sigaltstack(&stack, nullptr);
  struct sigaction action{};
  action.sa_handler = onFatalSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_ONSTACK;
  for (const int signal_number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
    sigaction(signal_number, &action, nullptr);
#endif
  Logger::info("Crash reports go to " + directory().string());
}

fs::path write(std::string_view reason)
{
  auto& state = crashState();
  if (!state.prepared.load()) prepare();
  const fs::path report(state.report_path.data());
  if (!state.written.test_and_set())
  {
    writeReport(reason.data(), reason.size());
    Logger::error(
        std::format("Fatal error: {} (report: {})", reason, report.string()));
    Logger::flush();
  }
  return report;
}

std::optional<Pending> pending()
{
  const fs::path folder = directory();
  std::ifstream marker(folder / PENDING_FILE);
  std::string name;
  if (!marker || !std::getline(marker, name)) return std::nullopt;
  // Only a bare report name written by this game is trusted.
  if (!name.starts_with(REPORT_PREFIX) || !name.ends_with(".txt") ||
      name.find_first_of("/\\") != std::string::npos)
    return std::nullopt;
  std::error_code error;
  Pending crash;
  crash.report = folder / name;
  if (!fs::is_regular_file(crash.report, error)) return std::nullopt;
  if (const fs::path log = withLogExtension(crash.report);
      fs::is_regular_file(log, error))
    crash.log = log;
  return crash;
}

void acknowledge()
{
  const fs::path folder = directory();
  std::error_code error;
  fs::remove(folder / PENDING_FILE, error);

  std::vector<fs::path> reports;
  for (const auto& entry : fs::directory_iterator(folder, error))
  {
    const std::string name = entry.path().filename().string();
    if (name.starts_with(REPORT_PREFIX) && name.ends_with(".txt"))
      reports.push_back(entry.path());
  }
  if (reports.size() <= KEPT_REPORTS) return;
  // Names carry a sortable UTC stamp: newest first.
  std::ranges::sort(reports, std::greater<>{});
  for (std::size_t index = KEPT_REPORTS; index < reports.size(); ++index)
  {
    fs::remove(reports[index], error);
    fs::remove(withLogExtension(reports[index]), error);
  }
}
}  // namespace CrashReport
