// Plugin discovery, scanning and the plugin cache (plugin-cache.json).
// Scanning is performed by STGRScan.exe (a separate process) so that a
// crashing plugin cannot take down the GUI; the GUI only reads the cache.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace stgr::plugins {

struct PluginEntry {
    std::wstring path;
    std::wstring name;
    std::wstring vendor;
    int format = 0;          // 2 = VST2, 3 = VST3
    int version = 0;
    std::wstring uid;        // unique id (plugin-provided where available)
    int status = 0;          // 0=ok, 1=blacklisted, 2=scan failed
    long long lastScan = 0;  // unix seconds
};

// Default scan directories.
std::vector<std::wstring> default_vst3_dirs();
std::vector<std::wstring> default_vst2_dirs();

// Load the cache from plugin-cache.json. Returns false when absent.
bool load_plugin_cache(std::vector<PluginEntry>& out);

// Save the cache (atomic write).
bool save_plugin_cache(const std::vector<PluginEntry>& entries);

// Record a single plugin result (used by the scan process; merges into the
// existing cache file so a crash loses at most one entry).
bool merge_plugin_result(const PluginEntry& entry);

// Best-effort plugin metadata read WITHOUT instantiating the plugin
// (used for quick file browsing; returns false when unknown).
bool probe_plugin_file(const std::wstring& path, PluginEntry& out);

} // namespace stgr::plugins
