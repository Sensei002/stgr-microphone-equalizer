// STGR Microphone Equalizer - SFX (capture) Audio Processing Object.
//
// This DLL is the persistent microphone processing component. It runs inside
// the Windows audio engine and:
//   - loads the per-endpoint configuration (JSON) written by the GUI,
//   - runs the built-in DSP chain (gain, filters, EQ, dynamics, limiter),
//   - forwards plugin stages to the STGR Audio Server process through
//     shared memory (one-block pipeline) and consumes the result.
//
// The APO keeps processing after the GUI closes and after reboot (the audio
// engine instantiates it whenever the endpoint is used).
#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include "../dsp/engine.h"
#include "../bridge/shm.h"

namespace stgr::apo {

class StgrApo final : public IAudioProcessingObject,
                     public IAudioProcessingObjectConfiguration,
                     public IAudioProcessingObjectRT,
                     public IAudioSystemEffects,
                     public IAudioSystemEffects2,
                     public dsp::PluginBridgeSink {
public:
    StgrApo();
    ~StgrApo() override;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    // IAudioProcessingObject
    IFACEMETHODIMP Reset() override;
    IFACEMETHODIMP GetLatency(HNSTIME* pTime) override;
    IFACEMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override;
    IFACEMETHODIMP Initialize(UINT32 cbDataSize, BYTE* pbyData) override;
    IFACEMETHODIMP IsInputFormatSupported(IAudioMediaType* pOppositeFormat,
                                          IAudioMediaType* pRequestedInputFormat,
                                          IAudioMediaType** ppSupportedInputFormat) override;
    IFACEMETHODIMP IsOutputFormatSupported(IAudioMediaType* pOppositeFormat,
                                           IAudioMediaType* pRequestedOutputFormat,
                                           IAudioMediaType** ppSupportedOutputFormat) override;
    IFACEMETHODIMP GetInputChannelCount(UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectConfiguration
    IFACEMETHODIMP LockForProcess(UINT32 u32NumInputConnections,
                                  APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                  UINT32 u32NumOutputConnections,
                                  APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
    IFACEMETHODIMP UnlockForProcess() override;

    // IAudioProcessingObjectRT
    IFACEMETHODIMP_(void) APOProcess(UINT32 u32NumInputConnections,
                                     APO_CONNECTION_PROPERTY** ppInputConnections,
                                     UINT32 u32NumOutputConnections,
                                     APO_CONNECTION_PROPERTY** ppOutputConnections) override;
    IFACEMETHODIMP_(UINT32) CalcInputFrames(UINT32 u32OutputFrameCount) override { return u32OutputFrameCount; }
    IFACEMETHODIMP_(UINT32) CalcOutputFrames(UINT32 u32InputFrameCount) override { return u32InputFrameCount; }

    // IAudioSystemEffects (marker interface - the engine recognizes the DLL
    // as a system effects APO because we also expose IAudioSystemEffects2).
    // IAudioSystemEffects2
    IFACEMETHODIMP GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) override;

    // dsp::PluginBridgeSink (called from the real-time path)
    bool process_plugin(const std::string& instanceId, float* buf,
                        int frames, int channels) override;

private:
    void start_watcher();
    void watcher_loop();
    void open_bridge();
    void close_bridge();
    void refresh_bridge_rings();
    void bridge_resync();

    std::atomic<ULONG> refCount_{1};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> locked_{false};
    std::atomic<bool> bypassMaster_{false};
    GUID processingMode_{STGR_MODE_DEFAULT};
    bool discoveryOnly_{false};

    std::wstring endpointId_;
    IPropertyStore* endpointProps_{nullptr};   // pAPOEndpointProperties
    IPropertyStore* sysFxProps_{nullptr};      // pAPOSystemEffectsProperties
    IMMDeviceCollection* deviceCollection_{nullptr};

    // Format cache (watcher thread owns the engine rebuild).
    std::atomic<UINT32> fmtSampleRate_{48000};
    std::atomic<UINT32> fmtChannels_{1};
    std::atomic<UINT32> fmtGen_{0};
    std::atomic<UINT32> appliedFmtGen_{0};
    std::atomic<UINT32> lastBlockFrames_{480};

    // DSP engine + config watch.
    std::unique_ptr<dsp::ProcessingEngine> engine_;
    std::thread watcher_;
    std::atomic<bool> watcherStop_{false};
    HANDLE cfgEvent_{nullptr};
    std::atomic<UINT64> procSeq_{0};

    // Plugin bridge (shared memory to the STGR Audio Server).
    bridge::SharedSection section_;
    bridge::ShmHeader* header_{nullptr};
    bridge::FrameRing inRing_;
    bridge::FrameRing outRing_;
    bridge::SeqRing inSeqRing_;
    bridge::SeqRing outSeqRing_;
    std::atomic<UINT64> pushSeq_{1};
    std::atomic<UINT64> lastConsumedSeq_{0};
    HANDLE effectsEvent_{nullptr};

    void ring_init();
    void bridge_resync();
};

} // namespace stgr::apo
