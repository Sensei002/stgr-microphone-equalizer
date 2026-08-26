#include "apo_registration.h"
#include "apo_guids.h"
#include "../common/version.h"
#include "../common/util.h"

#include <cwctype>
#include <winsvc.h>
#include <sddl.h>
#include <shlwapi.h>

namespace stgr::apo {

namespace {

// The property keys we write into the endpoint FxProperties store.
constexpr wchar_t kPKeyStreamEffectClsid[] = L"{D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},5";
constexpr wchar_t kPKeyAssociation[]       = L"{D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},0";
constexpr wchar_t kPKeySfxModes[]          = L"{D3993A3F-99C2-4402-B5EC-A92A0367664B},5";
constexpr wchar_t kModeDefault[]           = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
constexpr wchar_t kKsNodeTypeAny[]         = L"{00000000-0000-0000-0000-000000000000}";

constexpr wchar_t kClsidKeyPath[] =
    L"SOFTWARE\\Classes\\CLSID\\{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}";
constexpr wchar_t kAeKeyPath[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AudioEngine\\AudioProcessingObjects"
    L"\\{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}";

constexpr wchar_t kCaptureKey[] = L"SYSTEM\\CurrentControlSet\\Control\\MMDevices\\Audio\\Capture";
constexpr wchar_t kCaptureKeyAlt[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture";
constexpr wchar_t kFxProperties[] = L"FxProperties";

// Writes the AudioEngine\AudioProcessingObjects registration values.
void write_ae_registration()
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kAeKeyPath, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;

    const wchar_t* friendly = L"STGR Microphone Equalizer APO";
    const wchar_t* copyright = L"Copyright (c) STGR";
    RegSetValueExW(hk, L"FriendlyName", 0, REG_SZ, (const BYTE*)friendly, (DWORD)(wcslen(friendly) + 1) * sizeof(wchar_t));
    RegSetValueExW(hk, L"Copyright", 0, REG_SZ, (const BYTE*)copyright, (DWORD)(wcslen(copyright) + 1) * sizeof(wchar_t));

    // APO_FLAG_DEFAULT = samples/frame, frames/sec and bits/sample must match.
    const DWORD flags = 0xE;
    const DWORD one = 1;
    const DWORD maxInstances = 0xFFFFFFFF;
    RegSetValueExW(hk, L"MajorVersion", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"MinorVersion", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"Flags", 0, REG_DWORD, (const BYTE*)&flags, sizeof(flags));
    RegSetValueExW(hk, L"MinInputConnections", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"MaxInputConnections", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"MinOutputConnections", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"MaxOutputConnections", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(hk, L"MaxInstances", 0, REG_DWORD, (const BYTE*)&maxInstances, sizeof(maxInstances));
    RegSetValueExW(hk, L"NumAPOInterfaces", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    // Primary interface: IAudioProcessingObject {FD7F2B29-24D0-4B5C-B177-592C39F9CA10}
    const wchar_t* iface = L"{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}";
    RegSetValueExW(hk, L"APOInterface0", 0, REG_SZ, (const BYTE*)iface, (DWORD)(wcslen(iface) + 1) * sizeof(wchar_t));

    RegCloseKey(hk);
}

// Writes the FxProperties for one endpoint under the given root.
void write_endpoint_fx(HKEY root, const std::wstring& endpointKey)
{
    const std::wstring path = endpointKey + L"\\" + kFxProperties;
    HKEY hk = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;

    const std::wstring clsid = STGR_APO_CLSID_STRING;
    RegSetValueExW(hk, kPKeyStreamEffectClsid, 0, REG_SZ, (const BYTE*)clsid.c_str(),
                   (DWORD)(clsid.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(hk, kPKeyAssociation, 0, REG_SZ, (const BYTE*)kKsNodeTypeAny,
                   (DWORD)(wcslen(kKsNodeTypeAny) + 1) * sizeof(wchar_t));
    const wchar_t* modes[] = { kModeDefault };
    RegSetValueExW(hk, kPKeySfxModes, 0, REG_MULTI_SZ, (const BYTE*)modes, sizeof(modes));

    RegCloseKey(hk);
}

// Removes our stream-effect CLSID from the endpoint FxProperties.
void remove_endpoint_fx(HKEY root, const std::wstring& endpointKey)
{
    const std::wstring path = endpointKey + L"\\" + kFxProperties;
    HKEY hk = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_WRITE, &hk) != ERROR_SUCCESS) return;
    RegDeleteValueW(hk, kPKeyStreamEffectClsid);
    RegDeleteValueW(hk, kPKeySfxModes);
    RegCloseKey(hk);
}

// Removes our stream-effect CLSID from every capture endpoint's FX store.
void cleanup_all_endpoints()
{
    for (const wchar_t* root : { kCaptureKey, kCaptureKeyAlt }) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &hk) != ERROR_SUCCESS)
            continue;

        wchar_t subKey[256];
        DWORD index = 0;
        while (RegEnumKeyW(hk, index++, subKey, 256) == ERROR_SUCCESS) {
            remove_endpoint_fx(HKEY_LOCAL_MACHINE, std::wstring(root) + L"\\" + subKey);
        }
        RegCloseKey(hk);
    }
}

} // namespace

std::wstring sanitize_endpoint_key(const std::wstring& id)
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

HRESULT register_apo(const std::wstring& dllPath)
{
    // 1. COM class registration.
    {
        HKEY hk = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidKeyPath, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
            return E_ACCESSDENIED;
        const wchar_t* desc = L"STGR Microphone Equalizer APO";
        RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)desc, (DWORD)(wcslen(desc) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);

        const std::wstring inproc = std::wstring(kClsidKeyPath) + L"\\InProcServer32";
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, inproc.c_str(), 0, nullptr, 0,
                            KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
            return E_ACCESSDENIED;
        RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)dllPath.c_str(),
                       (DWORD)(dllPath.size() + 1) * sizeof(wchar_t));
        const wchar_t* model = L"Both";
        RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (const BYTE*)model,
                       (DWORD)(wcslen(model) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);
    }

    // 2. AudioEngine APO registration.
    write_ae_registration();
    return S_OK;
}

HRESULT unregister_apo()
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKeyPath);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kAeKeyPath);
    cleanup_all_endpoints();
    return S_OK;
}

bool is_apo_registered()
{
    HKEY hk = nullptr;
    const bool ok = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClsidKeyPath, 0, KEY_READ, &hk) == ERROR_SUCCESS;
    if (ok) RegCloseKey(hk);
    return ok;
}

HRESULT attach_endpoint(const std::wstring& endpointId)
{
    if (endpointId.empty()) return E_INVALIDARG;
    const std::wstring key = sanitize_endpoint_key(endpointId);

    write_endpoint_fx(HKEY_LOCAL_MACHINE, std::wstring(kCaptureKey) + L"\\" + key);
    write_endpoint_fx(HKEY_LOCAL_MACHINE, std::wstring(kCaptureKeyAlt) + L"\\" + key);
    return S_OK;
}

HRESULT detach_endpoint(const std::wstring& endpointId)
{
    if (endpointId.empty()) return E_INVALIDARG;
    const std::wstring key = sanitize_endpoint_key(endpointId);

    remove_endpoint_fx(HKEY_LOCAL_MACHINE, std::wstring(kCaptureKey) + L"\\" + key);
    remove_endpoint_fx(HKEY_LOCAL_MACHINE, std::wstring(kCaptureKeyAlt) + L"\\" + key);
    return S_OK;
}

bool is_endpoint_attached(const std::wstring& endpointId)
{
    const std::wstring key = sanitize_endpoint_key(endpointId);
    const std::wstring path = std::wstring(kCaptureKey) + L"\\" + key + L"\\" + kFxProperties;
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[128]{};
    DWORD size = sizeof(buf);
    const bool found = RegQueryValueExW(hk, kPKeyStreamEffectClsid, nullptr, nullptr,
                                       (LPBYTE)buf, &size) == ERROR_SUCCESS;
    RegCloseKey(hk);
    return found;
}

HRESULT restart_audio_service()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return HRESULT_FROM_WIN32(GetLastError());

    SC_HANDLE svc = OpenServiceW(scm, L"Audiosrv", SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    SERVICE_STATUS status{};
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        const DWORD err = GetLastError();
        if (err != ERROR_SERVICE_NOT_ACTIVE) {
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return HRESULT_FROM_WIN32(err);
        }
    } else {
        // Wait for the stop to complete.
        for (int i = 0; i < 50 && status.dwCurrentState != SERVICE_STOPPED; ++i) {
            Sleep(200);
            QueryServiceStatus(svc, &status);
        }
    }

    if (!StartServiceW(svc, 0, nullptr)) {
        const DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return HRESULT_FROM_WIN32(err);
    }

    // Wait until it is running again.
    for (int i = 0; i < 50; ++i) {
        QueryServiceStatus(svc, &status);
        if (status.dwCurrentState == SERVICE_RUNNING) break;
        Sleep(200);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return S_OK;
}

} // namespace stgr::apo
