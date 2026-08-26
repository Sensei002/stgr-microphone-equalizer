// VST2 (VST 2.4 ABI) host. The ABI declarations below reproduce the
// published Steinberg VST 2.4 interface (the plugin ABI is a functional
// interface definition; no Steinberg SDK source is included).
#include "plugin_api.h"
#include "../common/util.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef VSTCALLBACK
#define VSTCALLBACK __stdcall
#endif

namespace stgr::plugins {

namespace vst2 {

// ---- ABI declarations (VST 2.4) -----------------------------------------

typedef intptr_t (VSTCALLBACK* VstHostCallback)(void* effect, int opcode,
                                                int index, intptr_t value,
                                                void* ptr, float opt);

struct VstEffect {
    void* magic;                     // plugin unique id (4 chars)
    VstHostCallback dispatcher;
    VstHostCallback process;         // deprecated (effProcess)
    void (*setParameter)(VstEffect*, int, float);
    float (*getParameter)(VstEffect*, int);
    int numPrograms;
    int numParams;
    int numInputs;
    int numOutputs;
    int flags;                       // effFlags*
    char reserved1[8];
    char* name;
    int uniqueID;
    int version;
    void (*processReplacing)(VstEffect*, float**, float**, int);
    void (*processDoubleReplacing)(VstEffect*, double**, double**, int);
    char reserved2[56];
    VstHostCallback audioMaster;
    int32_t realID;
    char* initialDelay;
    char* objectName;
    char* vendorString;
    char* productString;
    int vendorVersion;
    char* vendorUniqueID;
    void* reserved3[4];
};

// Opcodes used by this host.
enum {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effGetParamLabel = 16,
    effGetParamDisplay = 17,
    effGetParamName = 18,
    effSetParameter = 24,
    effGetParameter = 25,
    effProcess = 33,
    effProcessReplacing = 34,
    effGetChunk = 36,
    effSetChunk = 37,
    effGetInitialDelay = 40,
    effGetVendorString = 41,
    effGetProductString = 42,
    effGetVendorVersion = 43,
    effGetParameterProperties = 56,
    effGetParameterCount = 62,
    effCanBeAutomated = 63,
    effGetParamId = 69,
};

enum {
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsProgramChunks = 1 << 5,
    effFlagsIsSynth = 1 << 8,
};

typedef VstEffect* (*VstEntryFunc)(VstHostCallback host);

// --------------------------------------------------------------------------

class Vst2Processor final : public PluginProcessor {
public:
    explicit Vst2Processor(const std::wstring& path) : path_(path) {}
    ~Vst2Processor() override { shutdown(); }

    bool init(double sampleRate, int channels) override;
    void process(float* interleaved, int frames, int channels) override;
    int latency_samples() const override { return latency_; }
    std::vector<PluginParamInfo> parameters() override;
    bool set_parameter_value(const std::string& id, double normalized) override;
    std::vector<uint8_t> save_state() override;
    bool load_state(const std::vector<uint8_t>& data) override;
    bool valid() const override { return effect_ != nullptr; }

private:
    static intptr_t VSTCALLBACK host_callback(void* effect, int opcode, int index,
                                              intptr_t value, void* ptr, float opt);
    void shutdown();
    void set_parameter_raw(int index, float value);

    std::wstring path_;
    HMODULE module_ = nullptr;
    VstEffect* effect_ = nullptr;
    int latency_ = 0;
    int paramCount_ = 0;
    bool canReplacing_ = false;
    int numIn_ = 1, numOut_ = 1;

    // Planar conversion buffers.
    std::vector<float> scratchIn_, scratchOut_;
    std::vector<float*> inPtrs_, outPtrs_;
};

// --------------------------------------------------------------------------

intptr_t VSTCALLBACK Vst2Processor::host_callback(void*, int opcode, int, intptr_t,
                                                  void*, float)
{
    switch (opcode) {
        case 23: return 1; // audioMasterGetVendorVersion (placeholder)
        default: return 0;
    }
}

void Vst2Processor::set_parameter_raw(int index, float value)
{
    if (effect_ && effect_->setParameter && index >= 0 && index < paramCount_)
        effect_->setParameter(effect_, index, value);
}

bool Vst2Processor::init(double sampleRate, int channels)
{
    shutdown();
    module_ = LoadLibraryW(path_.c_str());
    if (!module_) return false;

    VstEntryFunc entry = (VstEntryFunc)GetProcAddress(module_, "VSTPluginMain");
    if (!entry) entry = (VstEntryFunc)GetProcAddress(module_, "main");
    if (!entry) {
        shutdown();
        return false;
    }

    effect_ = entry(&Vst2Processor::host_callback);
    if (!effect_ || !effect_->dispatcher) {
        shutdown();
        return false;
    }

    effect_->audioMaster = &Vst2Processor::host_callback;
    effect_->dispatcher(effect_, effOpen, 0, 0, nullptr, 0.0f);
    effect_->dispatcher(effect_, effSetSampleRate, 0, 0, nullptr, (float)sampleRate);
    effect_->dispatcher(effect_, effSetBlockSize, 0, 8192, nullptr, 0.0f);

    canReplacing_ = (effect_->flags & effFlagsCanReplacing) != 0;
    numIn_ = effect_->numInputs > 0 ? effect_->numInputs : 1;
    numOut_ = effect_->numOutputs > 0 ? effect_->numOutputs : 1;
    paramCount_ = effect_->numParams;
    latency_ = (int)effect_->dispatcher(effect_, effGetInitialDelay, 0, 0, nullptr, 0.0f);

    // Resume processing (mains on).
    effect_->dispatcher(effect_, effMainsChanged, 0, 1, nullptr, 0.0f);

    scratchIn_.resize((size_t)8192 * numIn_);
    scratchOut_.resize((size_t)8192 * numOut_);
    inPtrs_.resize(numIn_);
    outPtrs_.resize(numOut_);
    (void)channels;
    return true;
}

void Vst2Processor::process(float* interleaved, int frames, int channels)
{
    if (!effect_ || frames <= 0) return;
    if (frames > 8192) frames = 8192;

    // De-interleave into planar scratch buffers.
    for (int ch = 0; ch < numIn_; ++ch) {
        const int srcCh = (ch < channels) ? ch : ch % channels;
        for (int f = 0; f < frames; ++f) {
            scratchIn_[ch * frames + f] = interleaved[f * channels + srcCh];
        }
    }
    for (int ch = 0; ch < numIn_; ++ch) inPtrs_[ch] = scratchIn_.data() + ch * frames;
    for (int ch = 0; ch < numOut_; ++ch) outPtrs_[ch] = scratchOut_.data() + ch * frames;

    if (canReplacing_ && effect_->processReplacing) {
        effect_->processReplacing(effect_, inPtrs_.data(), outPtrs_.data(), frames);
    } else if (effect_->process) {
        effect_->process(effect_, inPtrs_.data(), outPtrs_.data(), frames);
    }

    // Re-interleave to bridge output (mono mixdown).
    for (int f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (int ch = 0; ch < numOut_; ++ch) sum += scratchOut_[ch * frames + f];
        const float mono = sum / numOut_;
        for (int ch = 0; ch < channels; ++ch)
            interleaved[f * channels + ch] = mono;
    }
}

std::vector<PluginParamInfo> Vst2Processor::parameters()
{
    std::vector<PluginParamInfo> out;
    if (!effect_) return out;
    for (int i = 0; i < paramCount_; ++i) {
        PluginParamInfo p;
        p.id = std::to_string(i);
        char name[64] = {};
        if (effect_->dispatcher(effect_, effGetParamName, i, 0, name, 0.0f) == 1) {
            p.name = name;
        } else {
            p.name = "Param " + std::to_string(i);
        }
        if (effect_->getParameter)
            p.value = effect_->getParameter(effect_, i);
        p.min = 0.0;
        p.max = 1.0;
        out.push_back(p);
    }
    return out;
}

bool Vst2Processor::set_parameter_value(const std::string& id, double normalized)
{
    if (!effect_) return false;
    const int idx = std::atoi(id.c_str());
    if (idx < 0 || idx >= paramCount_) return false;
    set_parameter_raw(idx, (float)normalized);
    return true;
}

std::vector<uint8_t> Vst2Processor::save_state()
{
    std::vector<uint8_t> out;
    if (!effect_) return out;
    const bool chunkMode = (effect_->flags & effFlagsProgramChunks) != 0;
    if (chunkMode) {
        void* chunk = nullptr;
        intptr_t size = effect_->dispatcher(effect_, effGetChunk, 0, 0, &chunk, 0.0f);
        if (size > 0 && chunk) {
            out.assign((uint8_t*)chunk, (uint8_t*)chunk + size);
        }
    } else {
        out.resize((size_t)paramCount_ * sizeof(float));
        float* values = (float*)out.data();
        for (int i = 0; i < paramCount_; ++i)
            values[i] = effect_->getParameter(effect_, i);
    }
    return out;
}

bool Vst2Processor::load_state(const std::vector<uint8_t>& data)
{
    if (!effect_ || data.empty()) return false;
    const bool chunkMode = (effect_->flags & effFlagsProgramChunks) != 0;
    if (chunkMode) {
        effect_->dispatcher(effect_, effSetChunk, 0, (intptr_t)data.size(),
                            (void*)data.data(), 0.0f);
    } else {
        const float* values = (const float*)data.data();
        const size_t n = data.size() / sizeof(float);
        for (size_t i = 0; i < n && (int)i < paramCount_; ++i)
            set_parameter_raw((int)i, values[i]);
    }
    return true;
}

void Vst2Processor::shutdown()
{
    if (effect_ && effect_->dispatcher) {
        effect_->dispatcher(effect_, effMainsChanged, 0, 0, nullptr, 0.0f);
        effect_->dispatcher(effect_, effClose, 0, 0, nullptr, 0.0f);
    }
    effect_ = nullptr;
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
    }
}

} // namespace vst2

std::unique_ptr<PluginProcessor> create_vst2(const std::wstring& path)
{
    return std::make_unique<vst2::Vst2Processor>(path);
}

} // namespace stgr::plugins
