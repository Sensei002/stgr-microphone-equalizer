#include "schema.h"
#include "../common/util.h"
#include <cstdlib>

namespace stgr::config {

using namespace dsp;

// Helper: access a JSON object field, returning default if missing.
static inline double json_num(const json::Value& obj, const std::string& key, double def = 0.0)
{
    try { return obj[key].as_number(); } catch (...) { return def; }
}
static inline bool json_bool(const json::Value& obj, const std::string& key, bool def = false)
{
    try { return obj[key].as_bool(); } catch (...) { return def; }
}
static inline std::string json_str(const json::Value& obj, const std::string& key, const std::string& def = "")
{
    try { return obj[key].as_string(); } catch (...) { return def; }
}

// ---------------------------------------------------------------------------
// FilterType <-> string
// ---------------------------------------------------------------------------
static const char* filter_type_str(FilterType t)
{
    switch (t) {
        case FilterType::Peaking:   return "Peaking";
        case FilterType::LowShelf:  return "LowShelf";
        case FilterType::HighShelf: return "HighShelf";
        case FilterType::HighPass:  return "HighPass";
        case FilterType::LowPass:   return "LowPass";
        case FilterType::Notch:     return "Notch";
    }
    return "Peaking";
}

static FilterType filter_type_from_str(const std::string& s)
{
    if (s == "LowShelf")  return FilterType::LowShelf;
    if (s == "HighShelf") return FilterType::HighShelf;
    if (s == "HighPass")  return FilterType::HighPass;
    if (s == "LowPass")   return FilterType::LowPass;
    if (s == "Notch")     return FilterType::Notch;
    return FilterType::Peaking;
}

// ---------------------------------------------------------------------------
// StageType <-> string (named config_* to avoid ADL ambiguity with
// dsp::stage_type_name from params.h)
// ---------------------------------------------------------------------------
static const char* config_stage_type_str(StageType t)
{
    switch (t) {
        case StageType::Gain:       return "Gain";
        case StageType::HighPass:   return "HighPass";
        case StageType::LowPass:    return "LowPass";
        case StageType::LowShelf:   return "LowShelf";
        case StageType::HighShelf:  return "HighShelf";
        case StageType::Peaking:    return "Peaking";
        case StageType::Notch:      return "Notch";
        case StageType::Eq10:       return "ParametricEQ";
        case StageType::Gate:       return "Gate";
        case StageType::Expander:   return "Expander";
        case StageType::Compressor: return "Compressor";
        case StageType::Limiter:    return "Limiter";
        case StageType::Plugin:     return "Plugin";
    }
    return "Gain";
}

static StageType config_stage_type_from_str(const std::string& s)
{
    if (s == "HighPass")  return StageType::HighPass;
    if (s == "LowPass")   return StageType::LowPass;
    if (s == "LowShelf")  return StageType::LowShelf;
    if (s == "HighShelf") return StageType::HighShelf;
    if (s == "Peaking")   return StageType::Peaking;
    if (s == "Notch")     return StageType::Notch;
    if (s == "ParametricEQ") return StageType::Eq10;
    if (s == "Gate")      return StageType::Gate;
    if (s == "Expander")  return StageType::Expander;
    if (s == "Compressor") return StageType::Compressor;
    if (s == "Limiter")   return StageType::Limiter;
    if (s == "Plugin")    return StageType::Plugin;
    return StageType::Gain;
}

// ---------------------------------------------------------------------------
// StageParams serialization
// ---------------------------------------------------------------------------
json::Value stage_params_to_json(const StageParams& p)
{
    auto obj = json::Value::object({
        {"type", json::Value::string(config_stage_type_str(p.type))},
        {"enabled", json::Value::boolean(p.enabled)},
        {"gainDb", json::Value::number(p.gainDb)},
        {"filterType", json::Value::string(filter_type_str(p.filterType))},
        {"freq", json::Value::number(p.freq)},
        {"q", json::Value::number(p.q)},
        {"biquadGainDb", json::Value::number(p.biquadGainDb)},
        {"thresholdDb", json::Value::number(p.thresholdDb)},
        {"ratio", json::Value::number(p.ratio)},
        {"attackMs", json::Value::number(p.attackMs)},
        {"releaseMs", json::Value::number(p.releaseMs)},
        {"holdMs", json::Value::number(p.holdMs)},
        {"rangeDb", json::Value::number(p.rangeDb)},
        {"makeupDb", json::Value::number(p.makeupDb)},
        {"kneeDb", json::Value::number(p.kneeDb)},
        {"ceilingDb", json::Value::number(p.ceilingDb)},
        {"limiterAttackMs", json::Value::number(p.limiterAttackMs)},
        {"limiterReleaseMs", json::Value::number(p.limiterReleaseMs)},
        {"pluginInstanceId", json::Value::string(p.pluginInstanceId)},
        {"pluginName", json::Value::string(p.pluginName)},
        {"pluginPath", json::Value::string(p.pluginPath)},
        {"pluginFormat", json::Value::number((double)p.pluginFormat)},
        {"pluginBypassed", json::Value::boolean(p.pluginBypassed)},
    });

    // EQ bands
    std::vector<json::Value> bands;
    for (int i = 0; i < kMaxEqBands; ++i) {
        const auto& b = p.bands[i];
        bands.push_back(json::Value::object({
            {"enabled", json::Value::boolean(b.enabled)},
            {"type", json::Value::string(filter_type_str(b.type))},
            {"freq", json::Value::number(b.freq)},
            {"gainDb", json::Value::number(b.gainDb)},
            {"q", json::Value::number(b.q)},
        }));
    }
    obj["bands"] = json::Value::array(std::move(bands));

    // Plugin params
    std::vector<json::Value> params;
    for (const auto& pp : p.pluginParams) {
        params.push_back(json::Value::object({
            {"name", json::Value::string(pp.first)},
            {"value", json::Value::number(pp.second)},
        }));
    }
    obj["pluginParams"] = json::Value::array(std::move(params));

    return obj;
}

StageParams stage_params_from_json(const json::Value& v)
{
    StageParams p;
    p.type = config_stage_type_from_str(json_str(v, "type", "Gain"));
    p.enabled = json_bool(v, "enabled", true);
    p.gainDb = (float)json_num(v, "gainDb", 0.0);
    p.filterType = filter_type_from_str(json_str(v, "filterType", "Peaking"));
    p.freq = (float)json_num(v, "freq", 1000.0);
    p.q = (float)json_num(v, "q", 0.707);
    p.biquadGainDb = (float)json_num(v, "biquadGainDb", 0.0);
    p.thresholdDb = (float)json_num(v, "thresholdDb", -30.0);
    p.ratio = (float)json_num(v, "ratio", 3.0);
    p.attackMs = (float)json_num(v, "attackMs", 5.0);
    p.releaseMs = (float)json_num(v, "releaseMs", 80.0);
    p.holdMs = (float)json_num(v, "holdMs", 30.0);
    p.rangeDb = (float)json_num(v, "rangeDb", -60.0);
    p.makeupDb = (float)json_num(v, "makeupDb", 0.0);
    p.kneeDb = (float)json_num(v, "kneeDb", 0.0);
    p.ceilingDb = (float)json_num(v, "ceilingDb", -1.0);
    p.limiterAttackMs = (float)json_num(v, "limiterAttackMs", 0.5);
    p.limiterReleaseMs = (float)json_num(v, "limiterReleaseMs", 80.0);
    p.pluginInstanceId = json_str(v, "pluginInstanceId");
    p.pluginName = json_str(v, "pluginName");
    p.pluginPath = json_str(v, "pluginPath");
    p.pluginFormat = (int)json_num(v, "pluginFormat", 0);
    p.pluginBypassed = json_bool(v, "pluginBypassed", false);

    // Bands
    try {
        const auto& bands = v["bands"];
        if (bands.is_array()) {
            const size_t n = bands.size() < kMaxEqBands ? bands.size() : kMaxEqBands;
            for (size_t i = 0; i < n; ++i) {
                const auto& bv = bands[i];
                auto& b = p.bands[i];
                b.enabled = json_bool(bv, "enabled", false);
                b.type = filter_type_from_str(json_str(bv, "type", "Peaking"));
                b.freq = (float)json_num(bv, "freq", 1000.0);
                b.gainDb = (float)json_num(bv, "gainDb", 0.0);
                b.q = (float)json_num(bv, "q", 0.707);
            }
        }
    } catch (...) {}

    // Plugin params
    try {
        const auto& params = v["pluginParams"];
        if (params.is_array()) {
            for (size_t i = 0; i < params.size(); ++i) {
                const auto& pv = params[i];
                p.pluginParams.push_back({json_str(pv, "name"), (float)json_num(pv, "value", 0.0)});
            }
        }
    } catch (...) {}

    return p;
}

// ---------------------------------------------------------------------------
// DeviceConfig
// ---------------------------------------------------------------------------
json::Value device_config_to_json(const DeviceConfig& cfg)
{
    std::vector<json::Value> chain;
    for (const auto& s : cfg.chain)
        chain.push_back(stage_params_to_json(s));

    return json::Value::object({
        {"version", json::Value::number((double)cfg.version)},
        {"endpointId", json::Value::string(cfg.endpointId)},
        {"endpointName", json::Value::string(cfg.endpointName)},
        {"enabled", json::Value::boolean(cfg.enabled)},
        {"chain", json::Value::array(std::move(chain))},
    });
}

DeviceConfig device_config_from_json(const json::Value& v)
{
    DeviceConfig cfg;
    cfg.version = v["version"].as_int();
    cfg.endpointId = json_str(v, "endpointId");
    cfg.endpointName = json_str(v, "endpointName");
    cfg.enabled = json_bool(v, "enabled", true);
    try {
        const auto& chain = v["chain"];
        if (chain.is_array()) {
            for (size_t i = 0; i < chain.size(); ++i) {
                cfg.chain.push_back(stage_params_from_json(chain[i]));
            }
        }
    } catch (...) {}
    return cfg;
}

// ---------------------------------------------------------------------------
// GlobalConfig
// ---------------------------------------------------------------------------
json::Value global_config_to_json(const GlobalConfig& cfg)
{
    std::vector<json::Value> paths;
    for (const auto& p : cfg.customVstPaths)
        paths.push_back(json::Value::string(p));

    return json::Value::object({
        {"version", json::Value::number((double)cfg.version)},
        {"processingEnabled", json::Value::boolean(cfg.processingEnabled)},
        {"customVstPaths", json::Value::array(std::move(paths))},
        {"startWithWindows", json::Value::boolean(cfg.startWithWindows)},
        {"autoApply", json::Value::boolean(cfg.autoApply)},
        {"bridgeLatencyFrames", json::Value::number((double)cfg.bridgeLatencyFrames)},
    });
}

GlobalConfig global_config_from_json(const json::Value& v)
{
    GlobalConfig cfg;
    cfg.version = json_num(v, "version", 1);
    cfg.processingEnabled = json_bool(v, "processingEnabled", true);
    cfg.startWithWindows = json_bool(v, "startWithWindows", false);
    cfg.autoApply = json_bool(v, "autoApply", true);
    cfg.bridgeLatencyFrames = (int)json_num(v, "bridgeLatencyFrames", 480);
    try {
        const auto& arr = v["customVstPaths"];
        if (arr.is_array()) {
            for (size_t i = 0; i < arr.size(); ++i)
                cfg.customVstPaths.push_back(arr[i].as_string());
        }
    } catch (...) {}
    return cfg;
}

// ---------------------------------------------------------------------------
// Preset
// ---------------------------------------------------------------------------
json::Value preset_to_json(const Preset& p)
{
    std::vector<json::Value> chain;
    for (const auto& s : p.chain)
        chain.push_back(stage_params_to_json(s));
    return json::Value::object({
        {"name", json::Value::string(p.name)},
        {"chain", json::Value::array(std::move(chain))},
    });
}

Preset preset_from_json(const json::Value& v)
{
    Preset p;
    p.name = json_str(v, "name");
    try {
        const auto& chain = v["chain"];
        if (chain.is_array()) {
            for (size_t i = 0; i < chain.size(); ++i)
                p.chain.push_back(stage_params_from_json(chain[i]));
        }
    } catch (...) {}
    return p;
}

// ---------------------------------------------------------------------------
// Default presets
// ---------------------------------------------------------------------------
std::vector<Preset> default_presets()
{
    Preset flat;
    flat.name = "Flat";
    // no stages (pass-through)

    Preset voice;
    voice.name = "Voice";
    {
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 80.0f;
        hp.q = 0.707f;
        voice.chain.push_back(hp);

        StageParams eq;
        eq.type = StageType::Eq10;
        eq.enabled = true;
        // moderate voice presence boost
        for (int i = 0; i < kMaxEqBands; ++i) eq.bands[i] = EqBand{};
        eq.bands[0] = {true, FilterType::LowShelf,  80,   0, 0.707};
        eq.bands[1] = {true, FilterType::Peaking, 120,  -1, 0.707};
        eq.bands[2] = {true, FilterType::Peaking, 200,  -2, 0.707};
        eq.bands[3] = {true, FilterType::Peaking, 400,   0, 0.707};
        eq.bands[4] = {true, FilterType::Peaking, 1000,  2, 0.707};
        eq.bands[5] = {true, FilterType::Peaking, 2000,  3, 0.707};
        eq.bands[6] = {true, FilterType::Peaking, 4000,  2, 0.707};
        eq.bands[7] = {true, FilterType::Peaking, 8000,  1, 0.707};
        eq.bands[8] = {true, FilterType::Peaking, 12000, 0, 0.707};
        eq.bands[9] = {false, FilterType::Peaking, 16000, 0, 0.707};
        voice.chain.push_back(eq);

        StageParams limiter;
        limiter.type = StageType::Limiter;
        limiter.enabled = true;
        limiter.ceilingDb = -1.0f;
        voice.chain.push_back(limiter);
    }

    Preset streaming;
    streaming.name = "Streaming";
    {
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 60.0f;
        streaming.chain.push_back(hp);

        StageParams comp;
        comp.type = StageType::Compressor;
        comp.enabled = true;
        comp.thresholdDb = -18.0f;
        comp.ratio = 4.0f;
        comp.attackMs = 2.0f;
        comp.releaseMs = 60.0f;
        comp.makeupDb = 3.0f;
        streaming.chain.push_back(comp);

        StageParams limiter;
        limiter.type = StageType::Limiter;
        limiter.enabled = true;
        limiter.ceilingDb = -0.5f;
        streaming.chain.push_back(limiter);
    }

    Preset podcast;
    podcast.name = "Podcast";
    {
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 100.0f;
        podcast.chain.push_back(hp);

        StageParams gate;
        gate.type = StageType::Gate;
        gate.enabled = true;
        gate.thresholdDb = -40.0f;
        gate.attackMs = 0.5f;
        gate.releaseMs = 150.0f;
        gate.holdMs = 50.0f;
        gate.rangeDb = -60.0f;
        podcast.chain.push_back(gate);

        StageParams comp;
        comp.type = StageType::Compressor;
        comp.enabled = true;
        comp.thresholdDb = -20.0f;
        comp.ratio = 3.0f;
        comp.attackMs = 5.0f;
        comp.releaseMs = 80.0f;
        podcast.chain.push_back(comp);

        StageParams limiter;
        limiter.type = StageType::Limiter;
        limiter.enabled = true;
        podcast.chain.push_back(limiter);
    }

    Preset discord;
    discord.name = "Discord";
    {
        // Copy podcast: gentle gate + compressor + limiter
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 80.0f;
        discord.chain.push_back(hp);

        StageParams comp;
        comp.type = StageType::Compressor;
        comp.enabled = true;
        comp.thresholdDb = -15.0f;
        comp.ratio = 3.0f;
        comp.attackMs = 2.0f;
        comp.releaseMs = 60.0f;
        comp.makeupDb = 2.0f;
        discord.chain.push_back(comp);

        StageParams limiter;
        limiter.type = StageType::Limiter;
        limiter.enabled = true;
        discord.chain.push_back(limiter);
    }

    Preset cleanVoice;
    cleanVoice.name = "Clean Voice";
    {
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 120.0f;
        cleanVoice.chain.push_back(hp);

        StageParams eq;
        eq.type = StageType::Eq10;
        eq.enabled = true;
        eq.bands[0] = {true, FilterType::HighPass, 120, 0, 0.707};
        eq.bands[1] = {true, FilterType::Peaking, 200, -2, 0.707};
        eq.bands[2] = {true, FilterType::Peaking, 400, -1, 0.707};
        eq.bands[3] = {true, FilterType::Peaking, 800, 0, 0.707};
        eq.bands[4] = {true, FilterType::Peaking, 1600, 2, 0.707};
        eq.bands[5] = {true, FilterType::Peaking, 3200, 3, 0.707};
        eq.bands[6] = {true, FilterType::Peaking, 6400, 1, 0.707};
        for (int i = 7; i < kMaxEqBands; ++i) eq.bands[i].enabled = false;
        cleanVoice.chain.push_back(eq);

        StageParams limiter;
        limiter.type = StageType::Limiter;
        limiter.enabled = true;
        cleanVoice.chain.push_back(limiter);
    }

    return {flat, voice, streaming, podcast, discord, cleanVoice};
}

} // namespace stgr::config