// STGR DSP processing engine.
#pragma once
#include "params.h"
#include "denormal.h"
#include <atomic>
#include <memory>
#include <vector>
#include <cstdint>

namespace stgr::dsp {

class PluginBridgeSink {
public:
    virtual ~PluginBridgeSink() = default;
    virtual bool process_plugin(const std::string& instanceId, float* buf,
                                int frames, int channels) = 0;
};

struct MeterReadout {
    std::atomic<float> inputPeak{0.0f};
    std::atomic<float> outputPeak{0.0f};
    std::atomic<std::uint32_t> generation{0};
};

class Processor {
public:
    virtual ~Processor() = default;
    virtual void process(float* buf, int frames, int channels) = 0;
    virtual void configure(double sampleRate, int channels) {}
    virtual double latency_seconds() const { return 0.0; }
    virtual const char* name() const = 0;
};

struct EngineConfig {
    double sampleRate = 48000.0;
    int channels = 1;
    std::uint32_t generation = 0;
    bool bypassed = false;
    bool enabled = true;
    std::vector<std::unique_ptr<Processor>> stages;
    double totalLatencySeconds = 0.0;
};

std::unique_ptr<EngineConfig> build_config(const std::vector<StageParams>& chain,
                                           double sampleRate, int channels,
                                           PluginBridgeSink* sink);

class ProcessingEngine {
public:
    ProcessingEngine();
    ~ProcessingEngine();

    // Replace the active config. 'retireStamp' is the caller's callback
    // sequence counter; the old config is kept alive until at least two
    // callbacks have passed (see reap()).
    void set_config(std::unique_ptr<EngineConfig> cfg, std::uint64_t retireStamp);

    // Free retired configs that have survived long enough. Called by the
    // config watcher thread.
    void reap(std::uint64_t currentSeq);

    void process(float* buf, int frames);
    MeterReadout& meters() { return meters_; }
    void reset();
    double latency_seconds() const;

private:
    struct RetiredConfig {
        EngineConfig* cfg;
        std::uint64_t stamp;
    };
    std::atomic<EngineConfig*> active_{nullptr};
    std::vector<RetiredConfig> retired_;
    std::uint64_t retireCounter_ = 0;
    MeterReadout meters_;
    ScopedFtz ftz_;
};

} // namespace stgr::dsp