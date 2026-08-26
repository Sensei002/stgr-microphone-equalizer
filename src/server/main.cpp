// STGRAudioServer.exe - persistent plugin bridge host.
//
// Runs headless (started at logon by the installer/GUI). For every endpoint
// whose configuration contains plugin stages it:
//   - creates (or opens) the shared-memory section for that endpoint,
//   - consumes blocks pushed by the APO, runs the VST/VST3 chain, and
//     pushes the processed result back.
//
// The server is intentionally separate from the GUI: closing the GUI does
// not stop plugin processing, and a plugin crash only kills the server
// (the APO then bypasses plugins until the server restarts).
#include <windows.h>
#include <process.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../common/log.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../common/version.h"
#include "../config/manager.h"
#include "../config/schema.h"
#include "../plugins/plugin_api.h"
#include "../plugins/plugin_loader.h"
#include "../bridge/shm.h"

namespace {

using namespace stgr;

struct EndpointServer {
    std::wstring safeId;
    std::wstring cfgPath;
    bridge::SharedSection section;
    HANDLE event = nullptr;
    HANDLE stopEvent = nullptr;
    std::thread thread;

    bridge::ShmHeader* header = nullptr;
    bridge::FrameRing inRing;
    bridge::FrameRing outRing;
    bridge::SeqRing inSeq;
    bridge::SeqRing outSeq;

    // Ring memory (stable across format changes; rings are re-initialized
    // with the current channel count when the format changes).
    float* dataIn = nullptr;
    float* dataOut = nullptr;
    std::atomic<uint64_t>* seqIn = nullptr;
    std::atomic<uint64_t>* seqOut = nullptr;

    // Chain state.
    std::vector<std::unique_ptr<plugins::PluginProcessor>> chain;
    std::vector<dsp::StageParams> chainParams;
    std::vector<float> scratch;
    ULONGLONG cfgStamp = 0;
    uint32_t lastSampleRate = 0;
    uint32_t lastChannels = 0;
    uint32_t lastBlock = 0;
    uint32_t lastResync = 0;
    uint64_t lastInSeq = 0;
    int64_t lastPluginLatencyUs = 0;
    bool chainReady = false;
    std::atomic<bool> stop{false};
};

// Load the plugin chain for an endpoint from its device config.
bool load_chain(EndpointServer& s)
{
    config::ConfigManager mgr;
    config::DeviceConfig cfg;
    if (!mgr.load_device(to_utf8(s.safeId), cfg)) {
        s.chain.clear();
        s.chainParams.clear();
        s.chainReady = false;
        return true; // no config = no plugins
    }

    std::vector<dsp::StageParams> params;
    for (const auto& st : cfg.chain) {
        if (st.type == dsp::StageType::Plugin && st.enabled && !st.pluginBypassed) {
            params.push_back(st);
        }
    }

    // Rebuild only when the set changed.
    if (params.size() == s.chainParams.size()) {
        bool same = true;
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i].pluginInstanceId != s.chainParams[i].pluginInstanceId ||
                params[i].pluginPath != s.chainParams[i].pluginPath) {
                same = false;
                break;
            }
        }
        if (same) {
            // Apply parameter changes (cheap, called when the config changes).
            for (size_t i = 0; i < params.size() && i < s.chain.size(); ++i) {
                for (const auto& pv : params[i].pluginParams) {
                    s.chain[i]->set_parameter_value(pv.first, pv.second);
                }
            }
            s.chainReady = s.chain.size() == params.size();
            return true;
        }
    }

    // Reload plugins.
    s.chainParams = std::move(params);
    s.chain.clear();
    for (const auto& st : s.chainParams) {
        auto proc = st.pluginFormat == 3 ? plugins::create_vst3(to_wide(st.pluginPath))
                                         : plugins::create_vst2(to_wide(st.pluginPath));
        if (!proc) {
            STGR_LOG_WARN(L"[%s] plugin load failed: %s", s.safeId.c_str(), to_wide(st.pluginName).c_str());
            continue;
        }
        if (!proc->init(s.lastSampleRate ? (double)s.lastSampleRate : 48000.0, (int)s.lastChannels)) {
            STGR_LOG_WARN(L"[%s] plugin init failed: %s", s.safeId.c_str(), to_wide(st.pluginName).c_str());
            continue;
        }
        // Restore saved parameters.
        for (const auto& pv : st.pluginParams) {
            proc->set_parameter_value(pv.first, pv.second);
        }
        s.chain.push_back(std::move(proc));
    }
    s.chainReady = true;
    STGR_LOG_INFO(L"[%s] plugin chain: %d active", s.safeId.c_str(), (int)s.chain.size());
    return true;
}

void endpoint_thread(EndpointServer& s)
{
    STGR_LOG_INFO(L"[%s] server thread started", s.safeId.c_str());
    HANDLE waiters[2] = {s.event, s.stopEvent};

    bridge::ShmHeader* h = s.header;
    while (!s.stop.load()) {
        const DWORD wait = WaitForMultipleObjects(2, waiters, FALSE, 1000);
        if (wait == WAIT_OBJECT_0 + 1) break;

        h = s.header;
        if (!h) continue;

        const uint32_t sampleRate = h->sampleRate.load(std::memory_order_acquire);
        const uint32_t channels = h->channels.load(std::memory_order_acquire);
        const uint32_t block = h->blockFrames.load(std::memory_order_acquire);
        if (sampleRate == 0 || channels == 0 || block == 0) continue;

        // Format change -> re-init rings + chain.
        if (sampleRate != s.lastSampleRate || channels != s.lastChannels) {
            s.lastSampleRate = sampleRate;
            s.lastChannels = channels;
            s.lastBlock = block;
            s.inRing.init(s.dataIn, bridge::kRingBlocks * bridge::kDefaultBlock, channels,
                          &h->inputWrite, &h->inputRead);
            s.outRing.init(s.dataOut, bridge::kRingBlocks * bridge::kDefaultBlock, channels,
                           &h->outputWrite, &h->outputRead);
            s.inSeq.init(s.seqIn, bridge::kRingBlocks * bridge::kDefaultBlock,
                         &h->inputWrite, &h->inputRead);
            s.outSeq.init(s.seqOut, bridge::kRingBlocks * bridge::kDefaultBlock,
                          &h->outputWrite, &h->outputRead);
            s.inRing.reset();
            s.outRing.reset();
            s.inSeq.reset();
            s.outSeq.reset();
            s.lastInSeq = 0;
            s.scratch.resize((size_t)block * channels);
            load_chain(s);
            h->serverState.store(bridge::ServerRunning, std::memory_order_release);
        }

        // APO-initiated resync -> mirror it.
        const uint32_t resync = h->resyncCount.load(std::memory_order_acquire);
        if (resync != s.lastResync) {
            s.lastResync = resync;
            s.inRing.reset();
            s.outRing.reset();
            s.inSeq.reset();
            s.outSeq.reset();
            s.lastInSeq = 0;
        }

        // Process as many complete blocks as available.
        const uint32_t cap = bridge::kRingBlocks * bridge::kDefaultBlock;
        int processed = 0;
        while (s.inRing.available() >= block && s.outRing.free() >= block && processed < cap / (block ? block : 1)) {
            const uint64_t seq = s.inSeq.head();
            if (!s.inRing.pull(s.scratch.data(), block)) break;
            s.inSeq.consume(block);

            if (s.lastInSeq != 0 && seq != s.lastInSeq + 1) {
                // Sequence discontinuity (APO resynced); mirror it.
                s.inRing.reset();
                s.outRing.reset();
                s.inSeq.reset();
                s.outSeq.reset();
                s.lastInSeq = 0;
                break;
            }
            s.lastInSeq = seq;

            // Run the plugin chain.
            if (s.chainReady) {
                for (auto& p : s.chain) {
                    if (p) p->process(s.scratch.data(), (int)block, (int)channels);
                }
            }

            s.outRing.push(s.scratch.data(), block);
            s.outSeq.push(seq, block);
            h->framesProcessed.fetch_add(block, std::memory_order_relaxed);
            ++processed;
        }

        // Report plugin latency.
        int64_t latencyUs = 0;
        if (s.chainReady && sampleRate > 0) {
            for (auto& p : s.chain) {
                if (p) latencyUs += (int64_t)((double)p->latency_samples() * 1e6 / sampleRate);
            }
        }
        if (latencyUs != s.lastPluginLatencyUs) {
            s.lastPluginLatencyUs = latencyUs;
            h->pluginLatencyUs.store((uint32_t)latencyUs, std::memory_order_relaxed);
            h->activePluginCount.store((uint32_t)s.chain.size(), std::memory_order_relaxed);
        }

        if (processed > 0) h->serverState.store(bridge::ServerRunning, std::memory_order_release);
    }

    h = s.header;
    if (h) h->serverState.store(bridge::ServerAbsent, std::memory_order_release);
    STGR_LOG_INFO(L"[%s] server thread stopped", s.safeId.c_str());
}

void start_endpoint(EndpointServer& s)
{
    s.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const uint32_t cap = bridge::kRingBlocks * bridge::kDefaultBlock;
    const auto lay = bridge::layout(bridge::kMaxChannels, cap);

    if (!s.section.open(bridge::shm_name(s.safeId), true, bridge::section_bytes(), FILE_MAP_ALL_ACCESS)) {
        STGR_LOG_ERROR(L"[%s] cannot create shared section", s.safeId.c_str());
        return;
    }
    s.header = (bridge::ShmHeader*)s.section.view();
    if (s.header->magic != bridge::kMagic) {
        memset(s.section.view(), 0, bridge::section_bytes());
        new (s.header) bridge::ShmHeader{};
    }
    s.header->magic = bridge::kMagic;
    s.header->version = bridge::kProtocolVersion;
    s.header->ringCapacityFrames = cap;
    s.header->serverState.store(bridge::ServerStarting, std::memory_order_release);

    s.seqIn = (std::atomic<uint64_t>*)((uint8_t*)s.section.view() + lay.seqIn);
    s.seqOut = (std::atomic<uint64_t>*)((uint8_t*)s.section.view() + lay.seqOut);
    s.dataIn = (float*)((uint8_t*)s.section.view() + lay.dataIn);
    s.dataOut = (float*)((uint8_t*)s.section.view() + lay.dataOut);

    s.inRing.init(s.dataIn, cap, 1, &s.header->inputWrite, &s.header->inputRead);
    s.outRing.init(s.dataOut, cap, 1, &s.header->outputWrite, &s.header->outputRead);
    s.inSeq.init(s.seqIn, cap, &s.header->inputWrite, &s.header->inputRead);
    s.outSeq.init(s.seqOut, cap, &s.header->outputWrite, &s.header->outputRead);
    s.inRing.reset();
    s.outRing.reset();
    s.inSeq.reset();
    s.outSeq.reset();

    s.event = CreateEventW(nullptr, FALSE, FALSE, bridge::event_name(s.safeId).c_str());
    s.thread = std::thread(endpoint_thread, std::ref(s));
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    log_init(L"server");
    STGR_LOG_INFO(L"STGR Audio Server %S starting", STGR_VERSION_STRING);

    // High-ish thread priority for the audio processing threads.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    std::vector<std::unique_ptr<EndpointServer>> endpoints;
    std::vector<std::wstring> known;
    ULONGLONG lastPoll = 0;

    // Main loop: discover endpoint configs with plugin chains.
    while (true) {
        // Poll the config directory every two seconds.
        const ULONGLONG now = GetTickCount64();
        if (now - lastPoll > 2000) {
            lastPoll = now;

            const std::wstring dir = devices_cfg_dir();
            const std::vector<std::wstring> files = list_files(dir, L".json");

            std::vector<std::wstring> current;
            for (const auto& file : files) {
                std::wstring base = file.substr(file.find_last_of(L"\\/") + 1);
                const size_t dot = base.find_last_of(L'.');
                if (dot != std::wstring::npos) base.resize(dot);
                current.push_back(base);

                // Check whether this config contains plugin stages.
                config::ConfigManager mgr;
                config::DeviceConfig cfg;
                if (!mgr.load_device_from_path(file, cfg)) continue;
                bool hasPlugins = false;
                for (const auto& st : cfg.chain) {
                    if (st.type == dsp::StageType::Plugin && st.enabled) { hasPlugins = true; break; }
                }
                if (!hasPlugins) continue;

                const auto it = std::find_if(endpoints.begin(), endpoints.end(),
                    [&](const std::unique_ptr<EndpointServer>& e) { return e->safeId == base; });
                if (it == endpoints.end()) {
                    auto s = std::make_unique<EndpointServer>();
                    s->safeId = base;
                    s->cfgPath = file;
                    start_endpoint(*s);
                    endpoints.push_back(std::move(s));
                    STGR_LOG_INFO(L"endpoint %s: bridge started", base.c_str());
                }
            }

            // Stop servers whose config no longer exists or has no plugins.
            for (auto it = endpoints.begin(); it != endpoints.end();) {
                const bool keep = std::find(current.begin(), current.end(), (*it)->safeId) != current.end();
                if (!keep) {
                    (*it)->stop.store(true);
                    SetEvent((*it)->stopEvent);
                    if ((*it)->thread.joinable()) (*it)->thread.join();
                    STGR_LOG_INFO(L"endpoint %s: bridge stopped", (*it)->safeId.c_str());
                    it = endpoints.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Also wake on any APO config change event.
        const HANDLE cfgEvent = CreateEventW(nullptr, FALSE, FALSE, STGR_EVENT_CFG);
        if (cfgEvent) {
            WaitForSingleObject(cfgEvent, 500);
            CloseHandle(cfgEvent);
        } else {
            Sleep(500);
        }
    }

    return 0;
}
