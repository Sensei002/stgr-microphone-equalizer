// STGR Microphone Equalizer - version and identity constants.
// Central place for product branding and versioning.
#pragma once

#define STGR_PRODUCT_NAME      L"STGR Microphone Equalizer"
#define STGR_PRODUCT_NAME_A    "STGR Microphone Equalizer"
#define STGR_BRAND             L"STGR"
#define STGR_BRAND_A           "STGR"
#define STGR_VERSION_MAJOR     1
#define STGR_VERSION_MINOR     0
#define STGR_VERSION_PATCH     0
#define STGR_VERSION_STRING    "1.0.0"
#define STGR_VERSION_STRING_W  L"1.0.0"

// Registry identity of the APO registration (audio effects).
// A single well-known GUID set is used for this product; change all of them
// together when shipping a new incompatible generation.
#define STGR_APO_CLSID_STRING       "{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}"
#define STGR_APO_CLSID_STRING_W    L"{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}"
#define STGR_APO_EFFECT_ID_STRING   "{A1B2C3D4-5E6F-4A7B-8C9D-0E1F2A3B4C5D}"

// Where the APO looks for its per-endpoint configuration.
#define STGR_CFG_ROOT_NAME   L"STGR"
#define STGR_CFG_DIR         L"\\config\\devices\\"
#define STGR_CFG_GLOBAL      L"global.json"
#define STGR_CFG_PRESETS_DIR L"\\config\\presets\\"
#define STGR_PLUGIN_CACHE    L"plugin-cache.json"

// Named kernel objects shared between the APO, the audio server and the GUI.
#define STGR_SHM_PREFIX       L"Local\\STGR_"
#define STGR_EVENT_CFG        L"Local\\STGR_CfgChanged"
#define STGR_EVENT_SERVER     L"Local\\STGR_ServerAlive"
#define STGR_MUTEX_CFG        L"Local\\STGR_CfgMutex"

// GUID strings used for endpoint FX registration (Windows audio property keys).
// PKEY_FX_StreamEffectClsid      {D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},5
// PKEY_FX_Association            {D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},0
// PKEY_SFX_ProcessingModes...    {D3993A3F-99C2-4402-B5EC-A92A0367664B},5
#define STGR_PK_FX_BASE         L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D}"
#define STGR_PK_FX_STREAM       L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},5"
#define STGR_PK_FX_ASSOC        L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},0"
#define STGR_PK_SFX_MODES       L"{D3993A3F-99C2-4402-B5EC-A92A0367664B},5"
#define STGR_KSNODETYPE_ANY     L"{00000000-0000-0000-0000-000000000000}"

// AudioEngine\AudioProcessingObjects registration value names.
#define STGR_AE_FRIENDLYNAME    L"FriendlyName"
#define STGR_AE_COPYRIGHT       L"Copyright"
#define STGR_AE_MAJOR           L"MajorVersion"
#define STGR_AE_MINOR           L"MinorVersion"
#define STGR_AE_FLAGS           L"Flags"
#define STGR_AE_MININ           L"MinInputConnections"
#define STGR_AE_MAXIN           L"MaxInputConnections"
#define STGR_AE_MINOUT          L"MinOutputConnections"
#define STGR_AE_MAXOUT          L"MaxOutputConnections"
#define STGR_AE_MAXINST         L"MaxInstances"
#define STGR_AE_NUMIF           L"NumAPOInterfaces"
#define STGR_AE_IF0             L"APOInterface0"
