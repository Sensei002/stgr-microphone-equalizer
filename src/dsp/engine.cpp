#include "engine.h"
#include "biquad.h"
#include "dynamics.h"
#include <cmath>

namespace stgr::dsp {

namespace {

// ---------------------------------------------------------------------------
class GainProcessor : public Processor {
public:
    explicit GainProcessor(const StageParams& p) : target_(db_to_linear(p.gainDb)) {}
    void configure(double sampleRate, int) override
    {
        smooth_.configure(sampleRate, 5.0f, 5.0f);
        smooth_.reset(1.0f);
    }
    void process(float* buf, int frames, int channels) override
    {
        if (target_ == 1.0f) return;
        for (int f = 0; f < frames; ++f) {
            const float g = smooth_.tick(target_);
            for (int ch = 0; ch < channels; ++ch)
                buf[f * channels + ch] *= g;
        }
    }
    const char* name() const override { return "Gain"; }
private:
    float target_ = 1.0f;
    SmoothGain smooth_;
};

// ---------------------------------------------------------------------------
class BiquadProcessor : public Processor {
public:
    BiquadProcessor(const StageParams& p, FilterType t)
        : type_(t), freq_(p.freq), q_(p.q), gainDb_(p.biquadGainDb) {}
    void configure(double sampleRate, int channels) override
    {
        coeffs_ = design_biquad(type_, freq_, gainDb_, q_, sampleRate);
        state_.assign(channels, BiquadState{});
    }
    void process(float* buf, int frames, int channels) override
    {
        if (coeffs_.is_identity()) return;
        for (int ch = 0; ch < channels; ++ch) {
            BiquadState& s = state_[ch];
            for (int f = 0; f < frames; ++f)
                buf[f * channels + ch] = s.process(coeffs_, buf[f * channels + ch]);
        }
    }
    const char* name() const override { return "Biquad"; }
private:
    FilterType type_;
    float freq_, q_, gainDb_;
    BiquadCoeffs coeffs_;
    std::vector<BiquadState> state_;
};

// ---------------------------------------------------------------------------
class Eq10Processor : public Processor {
public:
    explicit Eq10Processor(const StageParams& p)
    {
        for (int i = 0; i < kMaxEqBands; ++i) bands_[i] = p.bands[i];
    }
    void configure(double sampleRate, int channels) override
    {
        coeffs_.assign(channels, std::vector<BiquadCoeffs>(kMaxEqBands));
        z1_.assign(channels, std::vector<double>(kMaxEqBands, 0.0));
        z2_.assign(channels, std::vector<double>(kMaxEqBands, 0.0));
        for (int ch = 0; ch < channels; ++ch) {
            for (int i = 0; i < kMaxEqBands; ++i) {
                auto& c = coeffs_[ch][i];
                if (!bands_[i].enabled) { c.identity(); continue; }
                c = design_biquad(bands_[i].type, bands_[i].freq, bands_[i].gainDb, bands_[i].q, sampleRate);
            }
        }
    }
    void process(float* buf, int frames, int channels) override
    {
        for (int ch = 0; ch < channels; ++ch) {
            auto& z1 = z1_[ch];
            auto& z2 = z2_[ch];
            for (int f = 0; f < frames; ++f) {
                double y = buf[f * channels + ch];
                for (int i = 0; i < kMaxEqBands; ++i) {
                    const auto& c = coeffs_[ch][i];
                    if (c.is_identity()) continue;
                    const double x = y;
                    y = c.b0 * x + z1[i];
                    z1[i] = c.b1 * x - c.a1 * y + z2[i];
                    z2[i] = c.b2 * x - c.a2 * y;
                }
                buf[f * channels + ch] = (float)y;
            }
        }
    }
    const char* name() const override { return "Parametric EQ"; }
private:
    EqBand bands_[kMaxEqBands];
    std::vector<std::vector<BiquadCoeffs>> coeffs_;
    std::vector<std::vector<double>> z1_, z2_;
};

// ---------------------------------------------------------------------------
class GateProcessor : public Processor {
public:
    explicit GateProcessor(const StageParams& p) : p_(p) {}
    void configure(double sampleRate, int channels) override
    {
        gates_.resize(channels);
        for (auto& g : gates_)
            g.configure(sampleRate, p_.thresholdDb, p_.attackMs, p_.releaseMs, p_.holdMs, p_.rangeDb);
    }
    void process(float* buf, int frames, int channels) override
    {
        for (int ch = 0; ch < channels; ++ch) gates_[ch].process(buf, frames, channels, ch);
    }
    const char* name() const override { return "Noise Gate"; }
private:
    StageParams p_;
    std::vector<Gate> gates_;
};

class ExpanderProcessor : public Processor {
public:
    explicit ExpanderProcessor(const StageParams& p) : p_(p) {}
    void configure(double sampleRate, int channels) override
    {
        exps_.resize(channels);
        for (auto& e : exps_)
            e.configure(sampleRate, p_.thresholdDb, p_.ratio, p_.attackMs, p_.releaseMs, p_.rangeDb);
    }
    void process(float* buf, int frames, int channels) override
    {
        for (int ch = 0; ch < channels; ++ch) exps_[ch].process(buf, frames, channels, ch);
    }
    const char* name() const override { return "Expander"; }
private:
    StageParams p_;
    std::vector<Expander> exps_;
};

class CompressorProcessor : public Processor {
public:
    explicit CompressorProcessor(const StageParams& p) : p_(p) {}
    void configure(double sampleRate, int channels) override
    {
        comps_.resize(channels);
        for (auto& c : comps_)
            c.configure(sampleRate, p_.thresholdDb, p_.ratio, p_.attackMs, p_.releaseMs, p_.kneeDb, p_.makeupDb);
    }
    void process(float* buf, int frames, int channels) override
    {
        for (int ch = 0; ch < channels; ++ch) comps_[ch].process(buf, frames, channels, ch);
    }
    const char* name() const override { return "Compressor"; }
private:
    StageParams p_;
    std::vector<Compressor> comps_;
};

class LimiterProcessor : public Processor {
public:
    explicit LimiterProcessor(const StageParams& p) : p_(p) {}
    void configure(double sampleRate, int channels) override
    {
        lims_.resize(channels);
        for (auto& l : lims_)
            l.configure(sampleRate, p_.ceilingDb, p_.limiterAttackMs, p_.limiterReleaseMs);
    }
    void process(float* buf, int frames, int channels) override
    {
        for (int ch = 0; ch < channels; ++ch) lims_[ch].process(buf, frames, channels, ch);
    }
    const char* name() const override { return "Limiter"; }
private:
    StageParams p_;
    std::vector<Limiter> lims_;
};

class PluginProcessor : public Processor {
public:
    PluginProcessor(const StageParams& p, PluginBridgeSink* sink)
        : id_(p.pluginInstanceId), sink_(sink), bypassed_(p.pluginBypassed) {}
    void process(float* buf, int frames, int channels) override
    {
        if (bypassed_ || !sink_) return;
        sink_->process_plugin(id_, buf, frames, channels);
    }
    const char* name() const override { return "Plugin"; }
private:
    std::string id_;
    PluginBridgeSink* sink_;
    bool bypassed_;
};

} // namespace

// ---------------------------------------------------------------------------
std::unique_ptr<EngineConfig> build_config(const std::vector<StageParams>& chain,
                                           double sampleRate, int channels,
                                           PluginBridgeSink* sink)
{
    auto cfg = std::make_unique<EngineConfig>();
    cfg->sampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    cfg->channels = channels > 0 ? channels : 1;

    for (const auto& p : chain) {
        if (!p.enabled) continue;
        std::unique_ptr<Processor> proc;
        switch (p.type) {
            case StageType::Gain:       proc = std::make_unique<GainProcessor>(p); break;
            case StageType::HighPass:   proc = std::make_unique<BiquadProcessor>(p, FilterType::HighPass); break;
            case StageType::LowPass:    proc = std::make_unique<BiquadProcessor>(p, FilterType::LowPass); break;
            case StageType::LowShelf:   proc = std::make_unique<BiquadProcessor>(p, FilterType::LowShelf); break;
            case StageType::HighShelf:  proc = std::make_unique<BiquadProcessor>(p, FilterType::HighShelf); break;
            case StageType::Peaking:    proc = std::make_unique<BiquadProcessor>(p, FilterType::Peaking); break;
            case StageType::Notch:      proc = std::make_unique<BiquadProcessor>(p, FilterType::Notch); break;
            case StageType::Eq10:       proc = std::make_unique<Eq10Processor>(p); break;
            case StageType::Gate:       proc = std::make_unique<GateProcessor>(p); break;
            case StageType::Expander:   proc = std::make_unique<ExpanderProcessor>(p); break;
            case StageType::Compressor: proc = std::make_unique<CompressorProcessor>(p); break;
            case StageType::Limiter:    proc = std::make_unique<LimiterProcessor>(p); break;
            case StageType::Plugin:     proc = std::make_unique<PluginProcessor>(p, sink); break;
        }
        proc->configure(cfg->sampleRate, cfg->channels);
        cfg->stages.push_back(std::move(proc));
    }
    return cfg;
}

// ---------------------------------------------------------------------------
ProcessingEngine::ProcessingEngine()
{
    enable_flush_to_zero();
}

ProcessingEngine::~ProcessingEngine()
{
    delete active_.load();
}

void ProcessingEngine::set_config(std::unique_ptr<EngineConfig> cfg, std::uint64_t retireStamp)
{
    EngineConfig* newCfg = cfg.release();
    EngineConfig* old = active_.exchange(newCfg);
    if (old) {
        retired_.push_back(RetiredConfig{old, retireStamp});
        if (retired_.size() > 8) {
            // Bound the retire queue: config changes are rare.
            reap(retireStamp);
        }
    }
}

void ProcessingEngine::reap(std::uint64_t currentSeq)
{
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (currentSeq >= it->stamp + 2) {
            delete it->cfg;
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }
}

void ProcessingEngine::process(float* buf, int frames)
{
    EngineConfig* cfg = active_.load(std::memory_order_acquire);
    if (!cfg || !cfg->enabled || cfg->bypassed) return;
    const int ch = cfg->channels;
    for (auto& stage : cfg->stages)
        stage->process(buf, frames, ch);
}

void ProcessingEngine::reset()
{
    // Rebuild config resets; no separate action needed.
}

double ProcessingEngine::latency_seconds() const
{
    EngineConfig* cfg = active_.load(std::memory_order_acquire);
    return cfg ? cfg->totalLatencySeconds : 0.0;
}

} // namespace stgr::dsp