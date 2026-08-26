#include "device_manager.h"
#include "../common/util.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propkey.h>
#include <propsys.h>
#include <comdef.h>
#include <cwctype>
#include <vector>

namespace stgr::devices {

namespace {
constexpr wchar_t kMMDevicesCaptureKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture";
constexpr wchar_t kFxPropertiesKey[] = L"FxProperties";
} // namespace

static std::wstring prop_string(IPropertyStore* store, REFPROPERTYKEY key)
{
    PROPVARIANT pv{};
    if (FAILED(store->GetValue(key, &pv))) return L"";
    std::wstring result;
    if (pv.vt == VT_LPWSTR && pv.pwszVal) result = pv.pwszVal;
    else if (pv.vt == VT_LPSTR && pv.pszVal) result = to_wide(pv.pszVal);
    PropVariantClear(&pv);
    return result;
}

std::wstring DeviceManager::device_name(IMMDevice* device) const
{
    IPropertyStore* store = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store) return L"";
    std::wstring name = prop_string(store, PKEY_Device_FriendlyName);
    if (name.empty()) name = prop_string(store, PKEY_Device_DeviceDesc);
    store->Release();
    return name;
}

bool DeviceManager::enumerate(std::vector<DeviceInfo>& out)
{
    out.clear();

    ComScope com;
    if (FAILED(com.hr())) return false;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
        return false;
    }

    // Default devices (communications + console).
    IMMDevice* defComm = nullptr;
    IMMDevice* defConsole = nullptr;
    enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &defComm);
    enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defConsole);

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ALL, &collection);
    if (FAILED(hr)) {
        if (defComm) defComm->Release();
        if (defConsole) defConsole->Release();
        enumerator->Release();
        return false;
    }

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device))) continue;

        DeviceInfo info;
        wchar_t* idBuf = nullptr;
        if (SUCCEEDED(device->GetId(&idBuf)) && idBuf) {
            info.id = idBuf;
            CoTaskMemFree(idBuf);
        }

        info.name = device_name(device);
        DWORD state = 0;
        if (SUCCEEDED(device->GetState(&state))) info.state = (DeviceState)state;

        if (defComm) {
            wchar_t* defId = nullptr;
            if (SUCCEEDED(defComm->GetId(&defId)) && defId) {
                info.isDefault = (info.id == defId);
                CoTaskMemFree(defId);
            }
        }
        if (defConsole) {
            wchar_t* defId = nullptr;
            if (SUCCEEDED(defConsole->GetId(&defId)) && defId) {
                info.isDefaultConsole = (info.id == defId);
                CoTaskMemFree(defId);
            }
        }

        // Hardware id from the device instance registry key.
        {
            IPropertyStore* store = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
                info.deviceDesc = prop_string(store, PKEY_Device_DeviceDesc);
                store->Release();
            }
        }

        device->Release();
        out.push_back(std::move(info));
    }

    collection->Release();
    if (defComm) defComm->Release();
    if (defConsole) defConsole->Release();
    enumerator->Release();
    return true;
}

bool DeviceManager::is_stgr_attached(const std::wstring& endpointId)
{
    HKEY hKey = nullptr;
    const std::wstring path = std::wstring(kMMDevicesCaptureKey) + L"\\" + endpointId + L"\\" + kFxPropertiesKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    // PKEY_FX_StreamEffectClsid value name "{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},5"
    const wchar_t* fxStreamValue = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},5";
    wchar_t value[256] = {};
    DWORD size = sizeof(value);
    const bool found = RegQueryValueExW(hKey, fxStreamValue, nullptr, nullptr,
                                        (LPBYTE)value, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return found;
}

std::wstring DeviceManager::safe_id(const std::wstring& id)
{
    std::wstring out;
    for (wchar_t c : id) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'{' || c == L'}' || c == L':')
            out += c;
        else
            out += L'_';
    }
    return out;
}

} // namespace stgr::devices
