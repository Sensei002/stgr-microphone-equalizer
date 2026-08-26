#include "log.h"
#include "paths.h"
#include "util.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace stgr {

static FILE* g_logFile = nullptr;
static std::mutex g_logMutex;
static LogLevel g_level = LogLevel::Info;

static const wchar_t* level_name(LogLevel l)
{
    switch (l) {
        case LogLevel::Debug: return L"DBG";
        case LogLevel::Info:  return L"INF";
        case LogLevel::Warn:  return L"WRN";
        case LogLevel::Error: return L"ERR";
    }
    return L"???";
}

static void rotate_if_needed(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    CloseHandle(h);
    if (size.QuadPart > 1024 * 1024) {
        const std::wstring old = path + L".1";
        MoveFileW(path.c_str(), old.c_str());
    }
}

void log_init(const wchar_t* component)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) return;
    const std::wstring dir = log_dir();
    ensure_dir(dir);
    const std::wstring path = dir + L"\\stgr-" + component + L".log";
    rotate_if_needed(path);
    _wfopen_s(&g_logFile, path.c_str(), L"a, ccs=UTF-8");
}

void log_set_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_level = level;
}

void log_write(LogLevel level, const wchar_t* fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (level < g_level) return;
    if (!g_logFile) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t message[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(message, 2048, _TRUNCATE, fmt, args);
    va_end(args);

    fwprintf(g_logFile, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             level_name(level), message);
    fflush(g_logFile);
}

} // namespace stgr
