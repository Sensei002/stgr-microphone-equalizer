#include "paths.h"
#include <shlobj.h>
#include <cwctype>
#include <cstdlib>

namespace stgr {

static std::wstring module_dir()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    return path;
}

std::wstring install_dir()
{
    // Tests and local dev override.
    wchar_t env[512]{};
    if (GetEnvironmentVariableW(L"STGR_INSTALL_DIR", env, 512) > 0) {
        return std::wstring(env);
    }
    // Program Files layout: <install>\STGR\
    wchar_t pf[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, pf) == S_OK) {
        return std::wstring(pf) + L"\\STGR";
    }
    return module_dir();
}

std::wstring user_data_dir()
{
    wchar_t env[512]{};
    if (GetEnvironmentVariableW(L"STGR_USER_DATA_DIR", env, 512) > 0) {
        return std::wstring(env);
    }
    wchar_t appdata[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata) == S_OK) {
        return std::wstring(appdata) + L"\\STGR";
    }
    return module_dir() + L"\\data";
}

std::wstring program_data_dir()
{
    wchar_t env[512]{};
    if (GetEnvironmentVariableW(L"STGR_PROGRAM_DATA_DIR", env, 512) > 0) {
        return std::wstring(env);
    }
    wchar_t pd[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, pd) == S_OK) {
        return std::wstring(pd) + L"\\STGR";
    }
    return install_dir() + L"\\data";
}

std::wstring devices_cfg_dir()
{
    return program_data_dir() + L"\\config\\devices";
}

std::wstring device_cfg_path(const std::wstring& endpointId)
{
    // Endpoint IDs are long device-id strings; map them to a safe file name.
    std::wstring name;
    for (wchar_t c : endpointId) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'{' || c == L'}' || c == L':') {
            name += c;
        } else {
            name += L'_';
        }
    }
    return devices_cfg_dir() + L"\\" + name + L".json";
}

std::wstring global_cfg_path()
{
    return program_data_dir() + L"\\config\\global.json";
}

std::wstring presets_dir()
{
    return program_data_dir() + L"\\config\\presets";
}

std::wstring plugin_cache_path()
{
    return program_data_dir() + L"\\config\\plugin-cache.json";
}

std::wstring log_dir()
{
    return program_data_dir() + L"\\logs";
}

} // namespace stgr
