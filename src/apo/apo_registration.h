// APO registration helpers: COM CLSID registration, AudioEngine APO
// registration, and endpoint FX association (FxProperties). Used by
// DllRegisterServer/DllUnregisterServer and by the elevated helper
// (STGRAdmin.exe) which the installer and GUI invoke.
#pragma once
#include <windows.h>
#include <string>

namespace stgr::apo {

// Writes the COM class registration and the AudioEngine\AudioProcessingObjects
// registration for this APO. 'dllPath' is the full path of the APO DLL.
// Requires elevation.
HRESULT register_apo(const std::wstring& dllPath);

// Removes the COM and AudioEngine registrations. Requires elevation.
HRESULT unregister_apo();

// True when the COM + AudioEngine registration is present (read-only).
bool is_apo_registered();

// Attaches the STGR SFX APO to a capture endpoint by writing the endpoint
// FX property store (PKEY_FX_StreamEffectClsid + association + modes).
// Requires elevation.
HRESULT attach_endpoint(const std::wstring& endpointId);

// Removes STGR from the endpoint's FX property store. Requires elevation.
HRESULT detach_endpoint(const std::wstring& endpointId);

// Read-only check whether STGR is attached to the endpoint.
bool is_endpoint_attached(const std::wstring& endpointId);

// Restarts the Windows audio service (Audiosrv + AudioEndpointBuilder) so
// that endpoint FX changes take effect. Requires elevation. Returns S_OK
// when the service restarted; S_FALSE when it was already stopped/needed no
// restart.
HRESULT restart_audio_service();

// Converts an endpoint id into the registry-safe device key name.
std::wstring sanitize_endpoint_key(const std::wstring& id);

} // namespace stgr::apo
