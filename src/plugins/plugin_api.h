// Plugin hosting API used by the STGR Audio Server.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace stgr::plugins {

enum class PluginFormat : int {
    Vst2 = 2,
    Vst3 = 3,
};

// Parameter metadata exposed to the GUI.
struct PluginParamInfo {
    std::string id;      // stable identifier (index for vst2, id for vst3)
    std::string name;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    double value = 0.0;  // current normalized value
    int stepCount = 0;   // 0 = continuous
};

// One loaded plugin instance (real-time processing in the server process).
class PluginProcessor {
public:
    virtual ~PluginProcessor() = default;

    // Full initialization. Returns false when the plugin is unusable.
    virtual bool init(double sampleRate, int channels) = 0;

    // Process one interleaved block in place. 'channels' is the number of
    // interleaved channels in the buffer (the bridge channel count). The
    // plugin internally maps to its own channel layout.
    virtual void process(float* interleaved, int frames, int channels) = 0;

    // Latency the plugin reports (in samples).
    virtual int latency_samples() const = 0;

    // Parameter access.
    virtual std::vector<PluginParamInfo> parameters() = 0;
    virtual bool set_parameter_value(const std::string& id, double normalized) = 0;

    // Plugin state (binary, plugin-defined). Empty vector = no state.
    virtual std::vector<uint8_t> save_state() = 0;
    virtual bool load_state(const std::vector<uint8_t>& data) = 0;

    // True when the processor is fully functional (was init'd).
    virtual bool valid() const = 0;
};

// Factory functions; return nullptr when the format is unsupported or the
// DLL cannot be loaded.
std::unique_ptr<PluginProcessor> create_vst2(const std::wstring& path);
std::unique_ptr<PluginProcessor> create_vst3(const std::wstring& path);

} // namespace stgr::plugins
