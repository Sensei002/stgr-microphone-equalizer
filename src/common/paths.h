// Filesystem locations used by STGR components.
#pragma once
#include <windows.h>
#include <string>

namespace stgr {

// The directory where STGR is installed (e.g. C:\Program Files\STGR\...).
// Resolved from the module path of stgr_common consumers at runtime, but can
// be overridden with an environment variable (used by tests).
std::wstring install_dir();

// Directory for the configuration GUI/tray/server (per-user, no admin needed).
std::wstring user_data_dir();

// ProgramData root for machine-wide configuration shared with the APO
// (audiodg runs in a different session and needs machine-readable state).
std::wstring program_data_dir();

// The APO reads per-endpoint configuration from here.
std::wstring devices_cfg_dir();
std::wstring device_cfg_path(const std::wstring& endpointId);
std::wstring global_cfg_path();
std::wstring presets_dir();
std::wstring plugin_cache_path();
std::wstring log_dir();

} // namespace stgr
