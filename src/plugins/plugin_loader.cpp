#include "plugin_loader.h"
#include "plugin_api.h"
#include "../common/version.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../config/json.h"
#include <shlobj.h>
#include <winver.h>
#include <cstdlib>
#include <ctime>

namespace stgr::plugins {

std::vector<std::wstring> default_vst3_dirs()
{
    std::vector<std::wstring> dirs;
    wchar_t common[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, common) == S_OK)
        dirs.push_back(std::wstring(common) + L"\\Common Files\\VST3");
    wchar_t appdata[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata) == S_OK)
        dirs.push_back(std::wstring(appdata) + L"\\VST3");
    return dirs;
}

std::vector<std::wstring> default_vst2_dirs()
{
    std::vector<std::wstring> dirs;
    wchar_t common[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, common) == S_OK) {
        dirs.push_back(std::wstring(common) + L"\\Steinberg\\VstPlugins");
        dirs.push_back(std::wstring(common) + L"\\Common Files\\VST2");
    }
    wchar_t appdata[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata) == S_OK)
        dirs.push_back(std::wstring(appdata) + L"\\VST");
    wchar_t pf[512]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, pf) == S_OK)
        dirs.push_back(std::wstring(pf) + L"\\VSTPlugins");
    return dirs;
}

namespace {

json::Value entry_to_json(const PluginEntry& e)
{
    return json::Value::object({
        {"path", json::Value::string(to_utf8(e.path))},
        {"name", json::Value::string(to_utf8(e.name))},
        {"vendor", json::Value::string(to_utf8(e.vendor))},
        {"format", json::Value::number((double)e.format)},
        {"version", json::Value::number((double)e.version)},
        {"uid", json::Value::string(to_utf8(e.uid))},
        {"status", json::Value::number((double)e.status)},
        {"lastScan", json::Value::number((double)e.lastScan)},
    });
}

PluginEntry entry_from_json(const json::Value& v)
{
    PluginEntry e;
    try { e.path = to_wide(v["path"].as_string()); } catch (...) {}
    try { e.name = to_wide(v["name"].as_string()); } catch (...) {}
    try { e.vendor = to_wide(v["vendor"].as_string()); } catch (...) {}
    try { e.format = v["format"].as_int(); } catch (...) {}
    try { e.version = v["version"].as_int(); } catch (...) {}
    try { e.uid = to_wide(v["uid"].as_string()); } catch (...) {}
    try { e.status = v["status"].as_int(); } catch (...) {}
    try { e.lastScan = (long long)v["lastScan"].as_number(); } catch (...) {}
    return e;
}

} // namespace

bool load_plugin_cache(std::vector<PluginEntry>& out)
{
    std::string text;
    if (!read_file_text(plugin_cache_path(), text)) return false;
    try {
        const auto root = json::parse(text);
        const auto& arr = root["plugins"];
        out.clear();
        for (size_t i = 0; i < arr.size(); ++i)
            out.push_back(entry_from_json(arr[i]));
        return true;
    } catch (...) {
        return false;
    }
}

bool save_plugin_cache(const std::vector<PluginEntry>& entries)
{
    std::vector<json::Value> arr;
    for (const auto& e : entries) arr.push_back(entry_to_json(e));
    const json::Value root = json::Value::object({
        {"version", json::Value::number(1)},
        {"plugins", json::Value::array(std::move(arr))},
    });
    ensure_dir(program_data_dir() + L"\\config");
    return write_file_text(plugin_cache_path(), root.serialize(true));
}

bool merge_plugin_result(const PluginEntry& entry)
{
    std::vector<PluginEntry> cache;
    load_plugin_cache(cache);

    bool found = false;
    for (auto& e : cache) {
        if (iequals(e.path, entry.path)) {
            e = entry;
            found = true;
            break;
        }
    }
    if (!found) cache.push_back(entry);
    return save_plugin_cache(cache);
}

bool probe_plugin_file(const std::wstring& path, PluginEntry& out)
{
    out = PluginEntry{};
    out.path = path;
    out.lastScan = (long long)std::time(nullptr);

    const std::wstring lower = to_lower(path);
    const bool isVst3 = iends_with(lower, L".vst3");
    if (!isVst3 && !iends_with(lower, L".dll")) return false;

    // File metadata (version info) without loading the DLL into us.
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size > 0) {
        std::vector<uint8_t> data(size);
        if (GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
            struct LangInfo { WORD lang, codePage; };
            LangInfo* langs = nullptr;
            UINT langCount = 0;
            if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                               (void**)&langs, &langCount) && langCount >= 4) {
                wchar_t subBlock[64]{};
                swprintf(subBlock, 64, L"\\StringFileInfo\\%04x%04x\\ProductName",
                         langs[0].lang, langs[0].codePage);
                wchar_t* value = nullptr;
                UINT valueLen = 0;
                if (VerQueryValueW(data.data(), subBlock, (void**)&value, &valueLen) && valueLen > 0)
                    out.name = value;
                swprintf(subBlock, 64, L"\\StringFileInfo\\%04x%04x\\CompanyName",
                         langs[0].lang, langs[0].codePage);
                if (VerQueryValueW(data.data(), subBlock, (void**)&value, &valueLen) && valueLen > 0)
                    out.vendor = value;
                swprintf(subBlock, 64, L"\\StringFileInfo\\%04x%04x\\FileVersion",
                         langs[0].lang, langs[0].codePage);
                if (VerQueryValueW(data.data(), subBlock, (void**)&value, &valueLen) && valueLen > 0)
                    out.version = _wtoi(value);
            }
        }
    }
    if (out.name.empty()) {
        // Fall back to the file name without extension.
        const size_t slash = path.find_last_of(L"\\/");
        std::wstring base = path.substr(slash + 1);
        const size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base.resize(dot);
        out.name = base;
    }

    out.format = isVst3 ? 3 : 2;
    out.status = 0;
    return true;
}

} // namespace stgr::plugins
