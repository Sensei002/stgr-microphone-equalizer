// DeviceManager: enumerates Windows capture (microphone) endpoints using the
// MMDevice API and reports their state, default status, and whether STGR
// processing is attached to them.
#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <string>
#include <vector>
#include <functional>

namespace stgr::devices {

enum class DeviceState { Active = 0, Disabled = 1, NotPresent = 2, Unplugged = 3 };

struct DeviceInfo {
    std::wstring id;          // stable endpoint id (used as config key)
    std::wstring name;        // friendly name
    std::wstring deviceDesc;  // device description
    std::wstring hwId;        // hardware id
    DeviceState state = DeviceState::Active;
    bool isDefault = false;          // Windows default (communications) capture device
    bool isDefaultConsole = false;   // default console capture device
};

// Simple RAII COM initialize (CoInitializeEx once per thread).
class ComScope {
public:
    ComScope() { hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    HRESULT hr() const { return hr_; }
private:
    HRESULT hr_;
};

class DeviceManager {
public:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Enumerate all capture endpoints.
    bool enumerate(std::vector<DeviceInfo>& out);

    // True if the given endpoint id has STGR FX attached (read-only check
    // on the MMDevices registry).
    static bool is_stgr_attached(const std::wstring& endpointId);

    // Version of the endpoint id suitable for file names (config paths).
    static std::wstring safe_id(const std::wstring& id);

private:
    std::wstring device_name(IMMDevice* device) const;
};

} // namespace stgr::devices
