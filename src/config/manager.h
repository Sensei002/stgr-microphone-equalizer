// ConfigManager: reads/writes configuration and preset files.
// The APO watches these files and hot-reloads the chain.
#pragma once
#include "schema.h"
#include "../common/paths.h"
#include <string>
#include <vector>

namespace stgr::config {

class ConfigManager {
public:
    ConfigManager();

    // Load device configuration for an endpoint. Returns false if absent.
    bool load_device(const std::string& endpointId, DeviceConfig& out) const;
    // Load device config by endpoint id from file; empty when missing.
    bool load_device_from_path(const std::wstring& path, DeviceConfig& out) const;

    // Save (atomically: write temp + rename).
    bool save_device(const DeviceConfig& cfg) const;

    // Remove the per-endpoint config (used when uninstalling / resetting).
    bool remove_device(const std::string& endpointId) const;

    // Global config.
    bool load_global(GlobalConfig& out) const;
    bool save_global(const GlobalConfig& cfg) const;

    // Presets: list/save/load/remove.
    std::vector<std::string> list_presets() const;
    bool save_preset(const Preset& p) const;
    bool load_preset(const std::string& name, Preset& out) const;
    bool remove_preset(const std::string& name) const;

    // Ensure the required directories exist.
    void ensure_layout() const;

private:
    std::wstring devices_dir() const { return devices_cfg_dir(); }
    std::wstring presets_dir_path() const { return presets_dir(); }
};

} // namespace stgr::config
