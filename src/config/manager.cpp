#include "manager.h"
#include "json.h"
#include "../common/util.h"
#include <algorithm>
#include <cwctype>

namespace stgr::config {

ConfigManager::ConfigManager() {}

bool ConfigManager::load_device_from_path(const std::wstring& path, DeviceConfig& out) const
{
    std::string text;
    if (!read_file_text(path, text)) return false;
    try {
        out = device_config_from_json(json::parse(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::load_device(const std::string& endpointId, DeviceConfig& out) const
{
    return load_device_from_path(device_cfg_path(to_wide(endpointId)), out);
}

bool ConfigManager::save_device(const DeviceConfig& cfg) const
{
    ensure_layout();
    const std::wstring path = device_cfg_path(to_wide(cfg.endpointId));
    const std::string text = device_config_to_json(cfg).serialize(true);
    return write_file_text(path, text);
}

bool ConfigManager::remove_device(const std::string& endpointId) const
{
    const std::wstring path = device_cfg_path(to_wide(endpointId));
    if (!path_exists(path)) return true;
    return DeleteFileW(path.c_str()) != FALSE;
}

bool ConfigManager::load_global(GlobalConfig& out) const
{
    std::string text;
    if (!read_file_text(global_cfg_path(), text)) return false;
    try {
        out = global_config_from_json(json::parse(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::save_global(const GlobalConfig& cfg) const
{
    ensure_layout();
    return write_file_text(global_cfg_path(), global_config_to_json(cfg).serialize(true));
}

std::vector<std::string> ConfigManager::list_presets() const
{
    std::vector<std::string> names;
    for (const auto& path : list_files(presets_dir_path(), L".json")) {
        const size_t slash = path.find_last_of(L"\\/");
        std::wstring base = path.substr(slash + 1);
        const size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base.resize(dot);
        names.push_back(to_utf8(base));
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool ConfigManager::save_preset(const Preset& p) const
{
    ensure_layout();
    // Sanitize the name into a file name.
    std::wstring name;
    for (wchar_t c : to_wide(p.name)) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L' ') name += c;
        else name += L'_';
    }
    if (name.empty()) return false;
    const std::wstring path = presets_dir_path() + L"\\" + name + L".json";
    return write_file_text(path, preset_to_json(p).serialize(true));
}

bool ConfigManager::load_preset(const std::string& name, Preset& out) const
{
    const std::wstring path = presets_dir_path() + L"\\" + to_wide(name) + L".json";
    std::string text;
    if (!read_file_text(path, text)) return false;
    try {
        out = preset_from_json(json::parse(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::remove_preset(const std::string& name) const
{
    const std::wstring path = presets_dir_path() + L"\\" + to_wide(name) + L".json";
    if (!path_exists(path)) return false;
    return DeleteFileW(path.c_str()) != FALSE;
}

void ConfigManager::ensure_layout() const
{
    ensure_dir(devices_cfg_dir());
    ensure_dir(presets_dir_path());
    ensure_dir(log_dir());
}

} // namespace stgr::config
