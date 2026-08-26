#include "stgr_apo.h"
#include "apo_guids.h"
#include "../common/version.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../config/manager.h"
#include "../config/schema.h"
#include "../devices/device_manager.h"
#include <combaseapi.h>
#include <propsys.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <cstring>

namespace stgr::apo {

namespace {

constexpr uint32_t kFormatGenBump = 1;

// Extract sample rate / channels / bits from a WAVEFORMATEX (may be an
// EXTENSIBLE format).
struct FormatInfo {
    UINT32 sampleRate = 48000;
    UINT32 channels = 1;
    bool float32 = true;
};

FormatInfo read_format(const WAVEFORMATEX* fmt)
{
    FormatInfo info;
    if (!fmt) return info;
    info.sampleRate = fmt->nSamplesPerSec;
    info.channels = fmt->nChannels;
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        info.float32 = fmt->wBitsPerSample == 32;
    } else if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        info.float32 = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && fmt->wBitsPerSample == 32;
    } else {
        info.float32 = false;
    }
    return info;
}

bool is_float32_format(const WAVEFORMATEX* fmt)
{
    return read_format(fmt).float32;
}

} // namespace

// ---------------------------------------------------------------------------

StgrApo::StgrApo()
{
    engine_ = std::make_unique<dsp::ProcessingEngine>();
}

StgrApo::~StgrApo()
{
    watcherStop_.store(true);
    if (cfgEvent_) SetEvent(cfgEvent_);
    if (watcher_.joinable()) watcher_.join();
    if (cfgEvent_) CloseHandle(cfgEvent_);
    if (effectsEvent_) CloseHandle(effectsEvent_);
    close_bridge();
    if (endpointProps_) endpointProps_->Release();
    if (sysFxProps_) sysFxProps_->Release();
    if (deviceCollection_) deviceCollection_->Release();
}

IFACEMETHODIMP StgrApo::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == __uuidof(IAudioProcessingObject)) {
        *ppv = static_cast<IAudioProcessingObject*>(this);
    } else if (riid == __uuidof(IAudioProcessingObjectConfiguration)) {
        *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
    } else if (riid == __uuidof(IAudioProcessingObjectRT)) {
        *ppv = static_cast<IAudioProcessingObjectRT*>(this);
    } else if (riid == __uuidof(IAudioSystemEffects)) {
        *ppv = static_cast<IAudioSystemEffects*>(this);
    } else if (riid == __uuidof(IAudioSystemEffects2)) {
        *ppv = static_cast<IAudioSystemEffects2*>(this);
    } else {
        return E_NOINTERFACE;
    }
    static_cast<IUnknown*>(*ppv)->AddRef();
    return S_OK;
}

// ---------------------------------------------------------------------------
// IAudioProcessingObject
// ---------------------------------------------------------------------------

IFACEMETHODIMP StgrApo::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
    if (initialized_.load()) return APOERR_ALREADY_INITIALIZED;
    if ((cbDataSize != 0 && pbyData == nullptr) || (cbDataSize == 0 && pbyData != nullptr))
        return E_INVALIDARG;

    // Modern Windows passes APOInitSystemEffects3 / 2 / 1 for system effects
    // APOs; older pipelines may pass APOInitBaseStruct or nothing.
    // We use size-range checks to stay compatible with future struct extensions.
    if (cbDataSize >= sizeof(APOInitSystemEffects2)) {
        // v2/v3/v4: same fields at the beginning (pDeviceCollection, mode, etc.)
        const auto* init = reinterpret_cast<const APOInitSystemEffects2*>(pbyData);
        endpointProps_ = init->pAPOEndpointProperties;
        sysFxProps_ = init->pAPOSystemEffectsProperties;
        deviceCollection_ = init->pDeviceCollection;
        processingMode_ = init->AudioProcessingMode;
        discoveryOnly_ = init->InitializeForDiscoveryOnly != FALSE;
        if (endpointProps_) endpointProps_->AddRef();
        if (sysFxProps_) sysFxProps_->AddRef();
        if (deviceCollection_) deviceCollection_->AddRef();
    } else if (cbDataSize >= sizeof(APOInitSystemEffects)) {
        // v1
        const auto* init = reinterpret_cast<const APOInitSystemEffects*>(pbyData);
        endpointProps_ = init->pAPOEndpointProperties;
        sysFxProps_ = init->pAPOSystemEffectsProperties;
        deviceCollection_ = init->pDeviceCollection;
        processingMode_ = STGR_MODE_DEFAULT;
        if (endpointProps_) endpointProps_->AddRef();
        if (sysFxProps_) sysFxProps_->AddRef();
        if (deviceCollection_) deviceCollection_->AddRef();
    }
    // APOInitBaseStruct and other sizes: no data we need.

    // Resolve the endpoint id (used as the configuration key and bridge
    // section name). The last device in the collection is this endpoint.
    if (deviceCollection_) {
        UINT32 count = 0;
        if (SUCCEEDED(deviceCollection_->GetCount(&count)) && count > 0) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(deviceCollection_->Item(count - 1, &device)) && device) {
                LPWSTR idBuf = nullptr;
                if (SUCCEEDED(device->GetId(&idBuf)) && idBuf) {
                    endpointId_ = idBuf;
                    CoTaskMemFree(idBuf);
                }
                device->Release();
            }
        }
    }
    if (endpointId_.empty())     if (endpointId_.empty()) endpointId_ = L"_default";

    initialized_.store(true);
    start_watcher();
    return S_OK;
}

IFACEMETHODIMP StgrApo::GetInputChannelCount(UINT32* pu32ChannelCount)
{
    if (!pu32ChannelCount) return E_POINTER;
    *pu32ChannelCount = fmtChannels_.load();
    return S_OK;
}

IFACEMETHODIMP StgrApo::GetLatency(HNSTIME* pTime)
{
    if (!pTime) return E_POINTER;
    // Built-in DSP is effectively zero latency; the plugin bridge adds one
    // block (the plugin path runs one block behind in the audio server).
    double latencySec = 0.0;
    const UINT32 sr = fmtSampleRate_.load(std::memory_order_acquire);
    if (header_ && sr > 0) {
        latencySec = (double)lastBlockFrames_ / (double)sr;
    }
    *pTime = (HNSTIME)(latencySec * 1e7);
    return S_OK;
}

IFACEMETHODIMP StgrApo::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps)
{
    if (!ppRegProps) return E_POINTER;
    auto* props = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
    if (!props) return E_OUTOFMEMORY;

    props->clsid = CLSID_STGR_APO;
    props->Flags = (APO_FLAG)(APO_FLAG_SAMPLESPERFRAME_MUST_MATCH |
                              APO_FLAG_FRAMESPERSECOND_MUST_MATCH |
                              APO_FLAG_BITSPERSAMPLE_MUST_MATCH);
    wcscpy_s(props->szFriendlyName, L"STGR Microphone Equalizer APO");
    wcscpy_s(props->szCopyrightInfo, L"Copyright (c) STGR");
    props->u32MajorVersion = STGR_VERSION_MAJOR;
    props->u32MinorVersion = STGR_VERSION_MINOR;
    props->u32MinInputConnections = 1;
    props->u32MaxInputConnections = 1;
    props->u32MinOutputConnections = 1;
    props->u32MaxOutputConnections = 1;
    props->u32MaxInstances = 0xFFFFFFFF;
    props->u32NumAPOInterfaces = 1;
    props->iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);

    *ppRegProps = props;
    return S_OK;
}

IFACEMETHODIMP StgrApo::IsInputFormatSupported(IAudioMediaType* pOppositeFormat,
                                               IAudioMediaType* pRequestedInputFormat,
                                               IAudioMediaType** ppSupportedInputFormat)
{
    UNREFERENCED_PARAMETER(pOppositeFormat);
    if (ppSupportedInputFormat) *ppSupportedInputFormat = nullptr;
    if (!pRequestedInputFormat) return E_INVALIDARG;

    const WAVEFORMATEX* fmt = pRequestedInputFormat->GetAudioFormat();
    return is_float32_format(fmt) ? S_OK : APOERR_FORMAT_NOT_SUPPORTED;
}

IFACEMETHODIMP StgrApo::IsOutputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                IAudioMediaType* pRequestedOutputFormat,
                                                IAudioMediaType** ppSupportedOutputFormat)
{
    UNREFERENCED_PARAMETER(pOppositeFormat);
    if (ppSupportedOutputFormat) *ppSupportedOutputFormat = nullptr;
    if (!pRequestedOutputFormat) return E_INVALIDARG;

    const WAVEFORMATEX* fmt = pRequestedOutputFormat->GetAudioFormat();
    return is_float32_format(fmt) ? S_OK : APOERR_FORMAT_NOT_SUPPORTED;
}

IFACEMETHODIMP StgrApo::Reset()
{
    // The watcher rebuilds the engine on the next cycle.
    fmtGen_.fetch_add(1);
    return S_OK;
}

// ---------------------------------------------------------------------------
// IAudioProcessingObjectConfiguration
// ---------------------------------------------------------------------------

IFACEMETHODIMP StgrApo::LockForProcess(UINT32 u32NumInputConnections,
                                       APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                       UINT32 u32NumOutputConnections,
                                       APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    if (u32NumInputConnections != 1 || u32NumOutputConnections != 1)
        return APOERR_NUM_CONNECTIONS_INVALID;
    if (!ppInputConnections || !ppOutputConnections || !ppInputConnections[0] || !ppOutputConnections[0])
        return E_POINTER;

    // Cache the stream format (non-real-time path).
    IAudioMediaType* inType = ppInputConnections[0]->pFormat;
    const WAVEFORMATEX* fmt = inType ? inType->GetAudioFormat() : nullptr;
    const FormatInfo info = read_format(fmt);
    if (info.sampleRate != fmtSampleRate_.load() || info.channels != fmtChannels_.load()) {
        fmtSampleRate_.store(info.sampleRate);
        fmtChannels_.store(info.channels);
        fmtGen_.fetch_add(kFormatGenBump);
    }

    locked_.store(true);
    return S_OK;
}

IFACEMETHODIMP StgrApo::UnlockForProcess()
{
    locked_.store(false);
    return S_OK;
}

// ---------------------------------------------------------------------------
// IAudioProcessingObjectRT
// ---------------------------------------------------------------------------

IFACEMETHODIMP_(void) StgrApo::APOProcess(UINT32 u32NumInputConnections,
                                          APO_CONNECTION_PROPERTY** ppInputConnections,
                                          UINT32 u32NumOutputConnections,
                                          APO_CONNECTION_PROPERTY** ppOutputConnections)
{
    if (u32NumInputConnections < 1 || u32NumOutputConnections < 1) return;

    APO_CONNECTION_PROPERTY* in = ppInputConnections[0];
    APO_CONNECTION_PROPERTY* out = ppOutputConnections[0];

    const UINT32 frames = in->u32ValidFrameCount;
    if (frames == 0) {
        out->u32ValidFrameCount = 0;
        out->u32BufferFlags = BUFFER_INVALID;
        return;
    }

    // The audio engine pre-marked all buffers INVALID; we must handle the
    // documented input flags.
    if (in->u32BufferFlags == BUFFER_INVALID) {
        out->u32ValidFrameCount = frames;
        out->u32BufferFlags = BUFFER_INVALID;
        return;
    }

    procSeq_.fetch_add(1, std::memory_order_relaxed);

    // The real-time thread must not be slowed down by denormals.
    dsp::enable_flush_to_zero();

    float* buf = reinterpret_cast<float*>(in->pBuffer);
    const bool active = initialized_.load() && locked_.load() &&
                        !bypassMaster_.load() &&
                        !IsEqualGUID(processingMode_, GUID_NULL) &&
                        !IsEqualGUID(processingMode_, AUDIO_SIGNALPROCESSINGMODE_RAW);

    if (active) {
        const UINT32 ch = fmtChannels_.load(std::memory_order_acquire);
        const bool fmtReady = fmtGen_.load(std::memory_order_acquire) ==
                              appliedFmtGen_.load(std::memory_order_acquire);
        if (ch >= 1 && ch <= bridge::kMaxChannels && fmtReady) {
            lastBlockFrames_.store(frames, std::memory_order_relaxed);
            if (in->u32BufferFlags == BUFFER_SILENT)
                memset(buf, 0, (size_t)frames * ch * sizeof(float));
            engine_->process(buf, frames);
            out->u32BufferFlags = BUFFER_VALID;
        } else {
            // Format not yet re-built; pass through unprocessed.
            out->u32BufferFlags = in->u32BufferFlags;
        }
    } else {
        out->u32BufferFlags = in->u32BufferFlags;
    }

    // Publish meters.
    if (header_) {
        float peakIn = 0.0f, peakOut = 0.0f;
        const UINT32 ch = fmtChannels_.load();
        const size_t n = (size_t)frames * ch;
        for (size_t i = 0; i < n; ++i) {
            const float v = buf[i];
            const float a = v < 0.0f ? -v : v;
            if (a > peakIn) peakIn = a;
        }
        if (out->pBuffer != in->pBuffer) {
            const float* ob = reinterpret_cast<const float*>(out->pBuffer);
            for (size_t i = 0; i < n; ++i) {
                const float v = ob[i];
                const float a = v < 0.0f ? -v : v;
                if (a > peakOut) peakOut = a;
            }
        } else {
            peakOut = peakIn;
        }
        header_->meterInPeak.store(peakIn, std::memory_order_relaxed);
        header_->meterOutPeak.store(peakOut, std::memory_order_relaxed);
        header_->sampleRate.store(fmtSampleRate_.load(), std::memory_order_relaxed);
        header_->channels.store(fmtChannels_.load(), std::memory_order_relaxed);
        header_->blockFrames.store(lastBlockFrames_.load(), std::memory_order_relaxed);
    }

    // Copy to the output buffer when the engine does not process in place.
    if (out->pBuffer != in->pBuffer) {
        memcpy(reinterpret_cast<void*>(out->pBuffer), buf,
               (size_t)frames * fmtChannels_.load() * sizeof(float));
    }
    out->u32ValidFrameCount = frames;
}

// ---------------------------------------------------------------------------
// IAudioSystemEffects2
// ---------------------------------------------------------------------------

IFACEMETHODIMP StgrApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event)
{
    if (!ppEffectsIds || !pcEffects) return E_POINTER;
    *ppEffectsIds = nullptr;
    *pcEffects = 0;

    if (IsEqualGUID(processingMode_, AUDIO_SIGNALPROCESSINGMODE_RAW))
        return S_OK; // no effects in raw mode

    if (Event) {
        if (effectsEvent_) CloseHandle(effectsEvent_);
        if (DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &effectsEvent_,
                            EVENT_MODIFY_STATE, FALSE, 0)) {
            // Store duplicated handle; signaled when the effect state changes.
        }
    }

    GUID* list = (GUID*)CoTaskMemAlloc(sizeof(GUID));
    if (!list) return E_OUTOFMEMORY;
    list[0] = STGR_EFFECT_ID;
    *ppEffectsIds = list;
    *pcEffects = 1;
    return S_OK;
}

// ---------------------------------------------------------------------------
// dsp::PluginBridgeSink (real-time path)
// ---------------------------------------------------------------------------

bool StgrApo::process_plugin(const std::string& instanceId, float* buf,
                             int frames, int channels)
{
    UNREFERENCED_PARAMETER(instanceId);
    if (!header_ || !section_.valid()) return false;
    if (header_->serverState.load(std::memory_order_acquire) != bridge::ServerRunning)
        return false;
    if (channels <= 0 || channels > (int)bridge::kMaxChannels) return false;
    if ((uint32_t)channels != header_->channels.load(std::memory_order_acquire))
        return false;
    if (frames <= 0) return false;

    // 1. Push the current block with the next sequence number.
    const uint64_t seq = pushSeq_.load(std::memory_order_relaxed);
    if (!inRing_.push(buf, (uint32_t)frames)) return false; // full: bypass
    inSeqRing_.push(seq, (uint32_t)frames);
    pushSeq_.store(seq + 1, std::memory_order_relaxed);

    // 2. Pull the previous block's result (one-block pipeline).
    if (outRing_.available() < (uint32_t)frames) return false; // not ready: bypass
    const uint64_t expected = lastConsumedSeq_.load(std::memory_order_relaxed) + 1;
    const uint64_t headSeq = outSeqRing_.head();
    if (headSeq != expected) {
        bridge_resync();
        return false;
    }
    if (!outRing_.pull(buf, (uint32_t)frames)) return false;
    outSeqRing_.consume((uint32_t)frames);
    lastConsumedSeq_.store(seq, std::memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
// Watcher thread
// ---------------------------------------------------------------------------

void StgrApo::start_watcher()
{
    if (discoveryOnly_) return; // discovery instances do no processing
    cfgEvent_ = CreateEventW(nullptr, FALSE, FALSE, STGR_EVENT_CFG);
    if (!cfgEvent_) cfgEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    watcher_ = std::thread([this] { watcher_loop(); });
}

void StgrApo::watcher_loop()
{
    std::wstring cfgPath = device_cfg_path(endpointId_);
    ULONGLONG lastStamp = 0;
    UINT32 lastFmtGen = 0;
    bool hadPlugins = false;

    // First iteration happens immediately.
    while (!watcherStop_.load()) {
        // Format change?
        const UINT32 fmtGen = fmtGen_.load(std::memory_order_acquire);
        if (fmtGen != lastFmtGen) {
            lastFmtGen = fmtGen;
            lastStamp = 0; // force reload
            // The bridge ring stride depends on the channel count; re-init
            // when the section is already open.
            if (section_.valid()) {
                refresh_bridge_rings();
                bridge_resync();
            }
        }

        // Config change? (device config + global config)
        ULONGLONG stamp = 0;
        {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(cfgPath.c_str(), GetFileExInfoStandard, &fad)) {
                stamp = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                        fad.ftLastWriteTime.dwLowDateTime;
            }
            WIN32_FILE_ATTRIBUTE_DATA g{};
            if (GetFileAttributesExW(global_cfg_path().c_str(), GetFileExInfoStandard, &g)) {
                stamp ^= (((ULONGLONG)g.ftLastWriteTime.dwHighDateTime << 32) |
                          g.ftLastWriteTime.dwLowDateTime) ^ 0x9E3779B97F4A7C15ULL;
            }
        }
        if (stamp != lastStamp) {
            lastStamp = stamp;

            config::ConfigManager mgr;
            config::DeviceConfig cfg;
            config::GlobalConfig global;
            bool hasPlugins = false;
            std::vector<dsp::StageParams> chain;
            bool enabled = false;

            if (mgr.load_device(to_utf8(endpointId_), cfg)) {
                chain = cfg.chain;
                enabled = cfg.enabled;
                for (const auto& s : chain) {
                    if (s.type == dsp::StageType::Plugin && s.enabled) hasPlugins = true;
                }
            }
            if (mgr.load_global(global)) {
                enabled = enabled && global.processingEnabled;
            }

            auto engineCfg = dsp::build_config(chain, fmtSampleRate_.load(),
                                               (int)fmtChannels_.load(),
                                               hasPlugins ? this : nullptr);
            engineCfg->enabled = enabled;
            engineCfg->generation = (uint32_t)stamp;
            appliedFmtGen_.store(fmtGen_.load(std::memory_order_acquire));

            engine_->set_config(std::move(engineCfg), procSeq_.load(std::memory_order_relaxed));
            engine_->reap(procSeq_.load(std::memory_order_relaxed));

            // Manage the plugin bridge.
            if (hasPlugins && !section_.valid()) {
                open_bridge();
            } else if (!hasPlugins && section_.valid()) {
                close_bridge();
            }
            hadPlugins = hasPlugins;
        }

        // Wait for the config-change event or a periodic re-check.
        WaitForSingleObject(cfgEvent_, 1000);
    }
}

void StgrApo::open_bridge()
{
    close_bridge();

    const std::wstring id = devices::DeviceManager::safe_id(endpointId_);
    const uint32_t cap = bridge::kRingBlocks * bridge::kDefaultBlock;
    if (!section_.open(bridge::shm_name(id), false, bridge::section_bytes(), FILE_MAP_ALL_ACCESS))
        return;

    auto* hdr = (bridge::ShmHeader*)section_.view();
    if (hdr->magic != bridge::kMagic) {
        // Created by the server; if the magic is wrong the server is not
        // there yet - keep the mapping but verify on each process_plugin.
        section_.close();
        return;
    }
    header_ = hdr;
    refresh_bridge_rings();
    bridge_resync();
}

void StgrApo::refresh_bridge_rings()
{
    if (!header_) return;
    const uint32_t cap = bridge::kRingBlocks * bridge::kDefaultBlock;
    const uint32_t channels = fmtChannels_.load(std::memory_order_acquire);
    const auto lay = bridge::layout(bridge::kMaxChannels, cap);

    auto* seqIn = (std::atomic<uint64_t>*)((uint8_t*)section_.view() + lay.seqIn);
    auto* seqOut = (std::atomic<uint64_t>*)((uint8_t*)section_.view() + lay.seqOut);
    auto* dataIn = (float*)((uint8_t*)section_.view() + lay.dataIn);
    auto* dataOut = (float*)((uint8_t*)section_.view() + lay.dataOut);

    inRing_.init(dataIn, cap, channels, &header_->inputWrite, &header_->inputRead);
    outRing_.init(dataOut, cap, channels, &header_->outputWrite, &header_->outputRead);
    inSeqRing_.init(seqIn, cap, &header_->inputWrite, &header_->inputRead);
    outSeqRing_.init(seqOut, cap, &header_->outputWrite, &header_->outputRead);

    pushSeq_.store(1);
    lastConsumedSeq_.store(0);
}

void StgrApo::close_bridge()
{
    header_ = nullptr;
    section_.close();
}

void StgrApo::bridge_resync()
{
    if (!header_) return;
    inRing_.reset();
    outRing_.reset();
    inSeqRing_.reset();
    outSeqRing_.reset();
    pushSeq_.store(1);
    lastConsumedSeq_.store(0);
    header_->resyncCount += 1;
}

} // namespace stgr::apo
