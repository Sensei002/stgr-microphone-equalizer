// Minimal file-based logging used by the GUI, server, tray and helper.
// The APO does NOT use this (no file I/O in the audio path); it only logs
// via the Windows event log helper in the APO module.
#pragma once
#include <windows.h>
#include <string>
#include <mutex>

namespace stgr {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Initializes logging to <ProgramData>\STGR\logs\stgr-<component>.log
// with rotation at 1 MiB.
void log_init(const wchar_t* component);
void log_set_level(LogLevel level);
void log_write(LogLevel level, const wchar_t* fmt, ...);

#define STGR_LOG_INFO(...)  ::stgr::log_write(::stgr::LogLevel::Info,  __VA_ARGS__)
#define STGR_LOG_WARN(...)  ::stgr::log_write(::stgr::LogLevel::Warn,  __VA_ARGS__)
#define STGR_LOG_ERROR(...) ::stgr::log_write(::stgr::LogLevel::Error, __VA_ARGS__)

} // namespace stgr
