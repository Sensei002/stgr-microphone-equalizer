// Shared parameter structures for the STGR DSP engine.
// These structs are plain data and are serialized by the configuration
// system (src/config) into JSON.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stgr::dsp {

// Filter types usable inside the EQ and as standalone stages.
enum class FilterType : std::int32_t {
    Peaking = 0,
    LowShelf = 1,
    HighShelf = 2,
    HighPass = 3,
    LowPass = 4,
    Notch = 5,
};

// A single parametric EQ band.
struct EqBand {
    bool enabled = false;
    FilterType type = FilterType::Peaking;
    float freq = 1000.0f;   // Hz
    float gainDb = 0.0f;    // dB
    float q = 0.707f;
};

// Stage types in the processing chain.
enum class StageType : std::int32_t {
    Gain = 0,
    HighPass = 1,
    LowPass = 2,
    LowShelf = 3,
    HighShelf = 4,
    Peaking = 5,
    Notch = 6,
    Eq10 = 7,          // 10-band parametric EQ
    Gate = 8,
    Expander = 9,
    Compressor = 10,
    Limiter = 11,
    Plugin = 12,       // VST/VST3 hosted by the audio server
};

inline const char* stage_type_name(StageType t)
{
    switch (t) {
        case StageType::Gain:       return "Gain";
        case StageType::HighPass:   return "High Pass";
        case StageType::LowPass:    return "Low Pass";
        case StageType::LowShelf:   return "Low Shelf";
        case StageType::HighShelf:  return "High Shelf";
        case StageType::Peaking:    return "Peaking";
        case StageType::Notch:      return "Notch";
        case StageType::Eq10:       return "Parametric EQ";
        case StageType::Gate:       return "Noise Gate";
        case StageType::Expander:   return "Expander";
        case StageType::Compressor: return "Compressor";
        case StageType::Limiter:    return "Limiter";
        case StageType::Plugin:     return "Plugin";
    }
    return "Unknown";
}

constexpr int kMaxEqBands = 10;

// Full parameter set for one chain stage. All fields are always present;
// unused fields for a given StageType are ignored.
struct StageParams {
    StageType type = StageType::Gain;
    bool enabled = true;

    // Gain / output level
    float gainDb = 0.0f;

    // Biquad stages
    FilterType filterType = FilterType::Peaking;
    float freq = 1000.0f;
    float q = 0.707f;
    float biquadGainDb = 0.0f;

    // 10-band EQ
    EqBand bands[kMaxEqBands];

    // Dynamics
    float thresholdDb = -30.0f; // gate/expander/compressor
    float ratio = 3.0f;         // expander/compressor
    float attackMs = 5.0f;
    float releaseMs = 80.0f;
    float holdMs = 30.0f;       // gate
    float rangeDb = -60.0f;     // gate/expander
    float makeupDb = 0.0f;      // compressor
    float kneeDb = 0.0f;        // compressor soft knee
    float ceilingDb = -1.0f;    // limiter
    float limiterAttackMs = 0.5f;
    float limiterReleaseMs = 80.0f;

    // Plugin slot (hosted by the audio server)
    std::string pluginInstanceId; // GUID assigned when the plugin is added
    std::string pluginName;
    std::string pluginPath;
    std::int32_t pluginFormat = 0; // 2 = VST2, 3 = VST3
    bool pluginBypassed = false;
    std::vector<std::pair<std::string, float>> pluginParams; // name -> normalized 0..1

    StageParams() { reset(); }

    void reset()
    {
        for (auto& b : bands) {
            b = EqBand{};
            b.enabled = false;
        }
        // Default EQ layout: a sensible voice curve is applied by presets;
        // the default configuration ships with a mild high-pass + flat EQ.
    }
};

} // namespace stgr::dsp
