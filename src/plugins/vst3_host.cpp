// VST3 host implementation. Requires the VST3 SDK headers.
#include "plugin_api.h"
#include <windows.h>
#include <cstring>
#include <vector>

#ifdef STGR_HAVE_VST3
// The VST3 headers define interface IID static members; INIT_CLASS_IID
// makes DECLARE_CLASS_IID emit both the TUID and the FUID definition in
// this TU (normally supplied by the SDK's base/source).
#define INIT_CLASS_IID
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vsttypes.h>

// The SDK declares FUnknownPrivate::atomicAdd in a header but implements it
// in base/source; provide the (identical) implementation here.
namespace Steinberg {
namespace FUnknownPrivate {
int32 PLUGIN_API atomicAdd(int32& value, int32 amount)
{
    const int32 old = (int32)InterlockedExchangeAdd((volatile LONG*)&value, amount);
    return old + amount;
}
} // namespace FUnknownPrivate
} // namespace Steinberg
#endif

namespace stgr::plugins {

#ifdef STGR_HAVE_VST3
namespace vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst; // IComponent, IAudioProcessor, AudioBusBuffers, ...

// --------------------------------------------------------------------------
// Local ABI constants (avoids dependency on SDK headers that may place or
// name these differently across versions).
static constexpr SpeakerArrangement kStgrMono = 0x4;
static constexpr MediaType          kMediaAudio = 0;
static constexpr BusDirection       kBusDirIn = 0;
static constexpr BusDirection       kBusDirOut = 1;

// VST3 "Audio Module Class" category string.
static const char* const kStgrAudioEffectClass = "Audio Module Class";

// ParameterInfo.title is UTF-16; narrow it for our config (ASCII plugin
// names are the norm; non-ASCII names degrade gracefully).
inline std::string narrow_name(const char16* s)
{
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i) {
        out += (char)(s[i] & 0x7F);
    }
    return out;
}

// Lightweight IBStream implementation over a vector<uint8_t>.
class MemoryStream : public IBStream {
public:
    explicit MemoryStream(std::vector<uint8_t>* data = nullptr)
        : ownData_(data == nullptr), data_(data ? data : &storage_)
    {
        if (ownData_) data_ = &storage_;
    }

    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override
    {
        if (!buffer) return kInvalidArgument;
        const int32 avail = (int32)(data_->size() - pos_);
        const int32 read = (numBytes < avail) ? numBytes : avail;
        if (read > 0) memcpy(buffer, data_->data() + pos_, read);
        pos_ += read;
        if (numBytesRead) *numBytesRead = read;
        return read > 0 ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override
    {
        if (!buffer) return kInvalidArgument;
        if (pos_ + numBytes > (int32)data_->size()) {
            data_->resize(pos_ + numBytes);
        }
        memcpy(data_->data() + pos_, buffer, numBytes);
        pos_ += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        switch (mode) {
            case kIBSeekSet: pos_ = (int32)pos; break;
            case kIBSeekCur: pos_ += (int32)pos; break;
            case kIBSeekEnd: pos_ = (int32)(data_->size() + pos); break;
        }
        if (pos_ < 0) pos_ = 0;
        if (pos_ > (int32)data_->size()) pos_ = (int32)data_->size();
        if (result) *result = pos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override
    {
        if (!pos) return kInvalidArgument;
        *pos = pos_;
        return kResultOk;
    }

    std::vector<uint8_t> detach_data()
    {
        if (ownData_) { std::vector<uint8_t> r = std::move(storage_); return r; }
        return {};
    }

private:
    int32 pos_ = 0;
    std::vector<uint8_t> storage_;
    std::vector<uint8_t>* data_;
    bool ownData_ = true;
};

IMPLEMENT_FUNKNOWN_METHODS(MemoryStream, IBStream, IBStream::iid)

// --------------------------------------------------------------------------

class Vst3Processor final : public PluginProcessor {
public:
    explicit Vst3Processor(const std::wstring& path) : path_(path) {}
    ~Vst3Processor() override { shutdown(); }

    bool init(double sampleRate, int channels) override;
    void process(float* interleaved, int frames, int channels) override;
    int latency_samples() const override { return latency_; }
    std::vector<PluginParamInfo> parameters() override;
    bool set_parameter_value(const std::string& id, double normalized) override;
    std::vector<uint8_t> save_state() override;
    bool load_state(const std::vector<uint8_t>& data) override;
    bool valid() const override { return component_ != nullptr; }

private:
    void shutdown();

    std::wstring path_;
    HMODULE module_ = nullptr;
    IPluginFactory* factory_ = nullptr;
    IComponent* component_ = nullptr;
    IAudioProcessor* processor_ = nullptr;
    IEditController* controller_ = nullptr;

    int latency_ = 0;
    int32 numInputs_ = 0, numOutputs_ = 0;
    int32 numChannelsIn_ = 0, numChannelsOut_ = 0;
    double sampleRate_ = 0;
    int blockSize_ = 0;

    // Planar buffers for the plugin.
    std::vector<float> inBuf_, outBuf_;
    std::vector<float*> inPtrs_, outPtrs_;
    AudioBusBuffers busIn_, busOut_;
};

// --------------------------------------------------------------------------

bool Vst3Processor::init(double sampleRate, int channels)
{
    shutdown();

    module_ = LoadLibraryW(path_.c_str());
    if (!module_) return false;

    auto getFactory = (IPluginFactory* (*)())GetProcAddress(module_, "GetPluginFactory");
    if (!getFactory) {
        shutdown();
        return false;
    }
    factory_ = getFactory();
    if (!factory_) { shutdown(); return false; }

    // Enumerate classes.
    PFactoryInfo factoryInfo;
    factory_->getFactoryInfo(&factoryInfo);
    const int32 count = factory_->countClasses();
    PClassInfo classInfo;
    bool found = false;
    for (int32 i = 0; i < count; ++i) {
        if (factory_->getClassInfo(i, &classInfo) != kResultOk) continue;
        if (strcmp(classInfo.category, kStgrAudioEffectClass) == 0) {
            found = true;
            break;
        }
    }
    if (!found) { shutdown(); return false; }

    // Create the component.
    FUnknown* unknown = nullptr;
    const TUID& unknownIID = FUnknown::iid.toTUID();
    if (factory_->createInstance(classInfo.cid, unknownIID, (void**)&unknown) != kResultOk) {
        shutdown();
        return false;
    }
    if (!unknown) { shutdown(); return false; }

    // Query IComponent + IAudioProcessor.
    {
        FUnknown* tmp = unknown;
        component_ = nullptr;
        if (tmp->queryInterface(IComponent::iid.toTUID(), (void**)&component_) != kResultOk)
            component_ = nullptr;
        processor_ = nullptr;
        if (tmp->queryInterface(IAudioProcessor::iid.toTUID(), (void**)&processor_) != kResultOk)
            processor_ = nullptr;
        controller_ = nullptr;
        (void)tmp->queryInterface(IEditController::iid.toTUID(), (void**)&controller_);
        tmp->release(); // release the initial ref; component_/processor_ each have +1
    }

    if (!processor_) { shutdown(); return false; }

    // Setup processing.
    sampleRate_ = sampleRate;
    blockSize_ = 1024;
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize_;
    setup.sampleRate = sampleRate;
    if (processor_->setupProcessing(setup) != kResultOk) {
        // Some plugins fail setupProcessing if they don't support the block size — adjust.
        setup.maxSamplesPerBlock = 512;
        (void)processor_->setupProcessing(setup);
    }

    // Get bus info.
    BusInfo busInfo;
    numInputs_ = 0;
    numOutputs_ = 0;
    if (component_) {
        for (int32 i = 0; i < component_->getBusCount(kMediaAudio, kBusDirIn); ++i) {
            if (component_->getBusInfo(kMediaAudio, kBusDirIn, i, busInfo) == kResultOk) {
                numInputs_++;
                numChannelsIn_ = busInfo.channelCount;
            }
        }
        for (int32 i = 0; i < component_->getBusCount(kMediaAudio, kBusDirOut); ++i) {
            if (component_->getBusInfo(kMediaAudio, kBusDirOut, i, busInfo) == kResultOk) {
                numOutputs_++;
                numChannelsOut_ = busInfo.channelCount;
            }
        }
    }
    if (numChannelsIn_ == 0) numChannelsIn_ = 1;
    if (numChannelsOut_ == 0) numChannelsOut_ = 1;

    // Set bus arrangements (mono in, mono out).
    SpeakerArrangement inArr[2] = {kStgrMono, kStgrMono};
    SpeakerArrangement outArr[2] = {kStgrMono, kStgrMono};
    processor_->setBusArrangements(inArr, numInputs_, outArr, numOutputs_);

    // Activate.
    if (component_) {
        component_->setActive(true);
        processor_->setProcessing(true);
    }

    // Latency.
    latency_ = (int)processor_->getLatencySamples();

    // Planar buffers.
    const int maxFrames = 8192;
    inBuf_.resize((size_t)maxFrames * numChannelsIn_);
    outBuf_.resize((size_t)maxFrames * numChannelsOut_);
    inPtrs_.resize(numChannelsIn_);
    outPtrs_.resize(numChannelsOut_);
    (void)channels;
    return true;
}

void Vst3Processor::process(float* interleaved, int frames, int channels)
{
    if (!processor_ || frames <= 0) return;
    if (frames > 8192) frames = 8192;

    // De-interleave into planar scratch buffers.
    for (int ch = 0; ch < numChannelsIn_; ++ch) {
        const int srcCh = (ch < channels) ? ch : ch % channels;
        float* dst = inBuf_.data() + ch * frames;
        for (int f = 0; f < frames; ++f) {
            dst[f] = interleaved[f * channels + srcCh];
        }
    }
    for (int ch = 0; ch < numChannelsIn_; ++ch)
        inPtrs_[ch] = inBuf_.data() + ch * frames;

    for (int ch = 0; ch < numChannelsOut_; ++ch)
        outPtrs_[ch] = outBuf_.data() + ch * frames;

    busIn_.numChannels = numChannelsIn_;
    busIn_.channelBuffers32 = inPtrs_.data();

    busOut_.numChannels = numChannelsOut_;
    busOut_.channelBuffers32 = outPtrs_.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.numSamples = frames;
    data.numInputs = numInputs_;
    data.numOutputs = numOutputs_;
    data.inputs = &busIn_;
    data.outputs = &busOut_;

    const tresult r = processor_->process(data);
    if (r != kResultOk) {
        // Plugin failed silently; output remains zero which is safe.
    }

    // Re-interleave to bridge output (mono mixdown).
    for (int f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannelsOut_; ++ch)
            sum += outBuf_[ch * frames + f];
        const float mono = sum / numChannelsOut_;
        for (int ch = 0; ch < channels; ++ch)
            interleaved[f * channels + ch] = mono;
    }
}

std::vector<PluginParamInfo> Vst3Processor::parameters()
{
    std::vector<PluginParamInfo> out;
    IEditController* ec = controller_;
    if (!ec) return out;

    int32 count = ec->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        PluginParamInfo p;
        p.id = std::to_string(i);
        ParameterInfo info{};
        if (ec->getParameterInfo(i, info) == kResultOk) {
            p.name = narrow_name(info.title);
            p.value = info.defaultNormalizedValue;
            p.def = info.defaultNormalizedValue;
            p.stepCount = info.stepCount;
        }
        if (p.name.empty()) p.name = "Param " + std::to_string(i);
        out.push_back(p);
    }
    return out;
}

bool Vst3Processor::set_parameter_value(const std::string& id, double normalized)
{
    if (!controller_) return false;
    const int32 idx = (int32)std::stol(id);
    if (idx < 0) return false;
    return controller_->setParamNormalized(idx, (ParamValue)normalized) == kResultOk;
}

std::vector<uint8_t> Vst3Processor::save_state()
{
    if (!component_) return {};
    MemoryStream stream;
    if (component_->getState(&stream) == kResultOk) {
        return stream.detach_data();
    }
    return {};
}

bool Vst3Processor::load_state(const std::vector<uint8_t>& data)
{
    if (!component_ || data.empty()) return false;
    MemoryStream stream(const_cast<std::vector<uint8_t>*>(&data));
    return component_->setState(&stream) == kResultOk;
}

void Vst3Processor::shutdown()
{
    if (component_) component_->setActive(false);
    if (processor_) processor_->setProcessing(false);
    if (component_) { component_->release(); component_ = nullptr; }
    if (processor_) { processor_->release(); processor_ = nullptr; }
    if (controller_) { controller_->release(); controller_ = nullptr; }
    if (factory_) { factory_->release(); factory_ = nullptr; }
    if (module_) { FreeLibrary(module_); module_ = nullptr; }
}

} // namespace vst3

std::unique_ptr<PluginProcessor> create_vst3(const std::wstring& path)
{
    return std::make_unique<vst3::Vst3Processor>(path);
}

#else // !STGR_HAVE_VST3

std::unique_ptr<PluginProcessor> create_vst3(const std::wstring& path)
{
    (void)path;
    return {};
}

#endif // STGR_HAVE_VST3

} // namespace stgr::plugins
