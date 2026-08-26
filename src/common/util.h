// Small dependency-free utilities shared across STGR components.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstdlib>

namespace stgr {

inline std::wstring to_wide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string to_utf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Case-insensitive wide string helpers.
inline bool iequals(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

inline bool iends_with(const std::wstring& s, const std::wstring& suffix)
{
    if (suffix.size() > s.size()) return false;
    return _wcsicmp(s.c_str() + (s.size() - suffix.size()), suffix.c_str()) == 0;
}

inline std::wstring to_lower(const std::wstring& s)
{
    std::wstring r = s;
    for (auto& c : r) c = towlower(c);
    return r;
}

// Replace first occurrence of 'from' with 'to'.
inline std::wstring replace_all(const std::wstring& s, const std::wstring& from, const std::wstring& to)
{
    std::wstring r = s;
    size_t pos = 0;
    while ((pos = r.find(from, pos)) != std::wstring::npos) {
        r.replace(pos, from.size(), to);
        pos += to.size();
    }
    return r;
}

// Format a double with the given number of decimals using the C locale
// independent of the system locale (dot decimal separator).
inline std::string fmt_double(double v, int decimals)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

inline std::wstring fmt_double_w(double v, int decimals)
{
    return to_wide(fmt_double(v, decimals));
}

inline std::wstring fmt_int(long long v)
{
    wchar_t buf[32];
    swprintf(buf, 32, L"%lld", v);
    return buf;
}

// Format an HRESULT as a readable hex string.
inline std::wstring fmt_hresult(HRESULT hr)
{
    wchar_t buf[48];
    swprintf(buf, 48, L"0x%08X", (unsigned)hr);
    return buf;
}

// Simple file read/write helpers (no CRT path issues).
inline bool read_file(const std::wstring& path, std::vector<uint8_t>& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 0) {
        out.resize((size_t)size.QuadPart);
        DWORD rd = 0;
        ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr);
        out.resize(rd);
    }
    CloseHandle(h);
    return true;
}

inline bool read_file_text(const std::wstring& path, std::string& out)
{
    std::vector<uint8_t> data;
    if (!read_file(path, data)) return false;
    out.assign(data.begin(), data.end());
    return true;
}

inline bool write_file(const std::wstring& path, const void* data, size_t len)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, data, (DWORD)len, &wr, nullptr);
    CloseHandle(h);
    return ok && wr == (DWORD)len;
}

inline bool write_file_text(const std::wstring& path, const std::string& text)
{
    return write_file(path, text.data(), text.size());
}

inline bool path_exists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

inline void ensure_dir(const std::wstring& dir)
{
    if (path_exists(dir)) return;
    // Create intermediate directories.
    std::wstring cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur += dir[i];
        if (dir[i] == L'\\' || dir[i] == L'/') {
            CreateDirectoryW(cur.c_str(), nullptr);
        }
    }
    CreateDirectoryW(dir.c_str(), nullptr);
}

// Enumerate one-level entries in a directory.
inline std::vector<std::wstring> list_files(const std::wstring& dir, const std::wstring& ext)
{
    std::vector<std::wstring> out;
    const std::wstring pattern = dir + L"\\*" + ext;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            out.push_back(dir + L"\\" + fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

} // namespace stgr
