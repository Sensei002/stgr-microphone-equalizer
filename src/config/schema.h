// Configuration schema: translates between JSON config files and the
// DSP chain parameters (StageParams).  Supports per-endpoint device config,
// global config, and presets.
#pragma once
#include "../dsp/params.h"
#include "../config/json.h"
#include <string>
#include <vector>

namespace stgr::config {

// ---------------------------------------------------------------------------
// Schema version
// ---------------------------------------------------------------------------
constexpr int kConfigVersion = 1;

// ---------------------------------------------------------------------------
// Device-specific configuration
// ---------------------------------------------------------------------------
struct DeviceConfig {
    int version = kConfigVersion;
    std::string endpointId;          // stable Windows device id
    std::string endpointName;        // display name cache
    bool enabled = true;
    std::vector<dsp::StageParams> chain;
};

// ---------------------------------------------------------------------------
// Global configuration
// ---------------------------------------------------------------------------
struct GlobalConfig {
    int version = kConfigVersion;
    bool processingEnabled = true;   // master switch (tray toggle)
    std::vector<std::string> customVstPaths; // additional VST/VST3 search dirs
    bool startWithWindows = false;
    bool autoApply = true;
    int bridgeLatencyFrames = 480; // plugin bridge buffer size (frames)
};

// ---------------------------------------------------------------------------
// Preset
// ---------------------------------------------------------------------------
struct Preset {
    std::string name;
    std::vector<dsp::StageParams> chain;
};

// ---------------------------------------------------------------------------
// JSON <-> Config conversion
// ---------------------------------------------------------------------------

// Serialize a single StageParams to JSON.
json::Value stage_params_to_json(const dsp::StageParams& p);

// Deserialize a single StageParams from JSON.
dsp::StageParams stage_params_from_json(const json::Value& v);

// Serialize a full device config.
json::Value device_config_to_json(const DeviceConfig& cfg);
DeviceConfig device_config_from_json(const json::Value& v);

// Global config.
json::Value global_config_to_json(const GlobalConfig& cfg);
GlobalConfig global_config_from_json(const json::Value& v);

// Preset.
json::Value preset_to_json(const Preset& p);
Preset preset_from_json(const json::Value& v);

// Built-in default presets.
std::vector<Preset> default_presets();

} // namespace stgr::config