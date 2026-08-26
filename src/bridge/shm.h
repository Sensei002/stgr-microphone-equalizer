// Shared-memory bridge protocol between the STGR APO (audiodg) and the
// STGR Audio Server (STGRAudioServer.exe), which hosts VST/VST3 plugins.
//
// Layout of each named section "Local\STGR_<endpoint>":
//   [ShmHeader] [seqIn] [dataIn] [seqOut] [dataOut]
//
// The APO pushes a block (tagged with a sequence number) into the input
// ring and consumes the previous block from the output ring (one-block
// pipeline).  The server consumes input, runs the plugin chain, and writes
// output tagged with the same sequence numbers, so both sides can detect
// resynchronization after a server restart.
//
// All indices are monotonic 64-bit counters (mod capacity, power of two).
// SPSC: the APO is the only writer of input and reader of output; the
// server is the only reader of input and writer of output.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace stgr::bridge {

constexpr uint32_t kMagic           = 0x53544752u; // 'STGR'
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kMaxChannels     = 8;
constexpr uint32_t kDefaultBlock    = 480;         // frames per APO callback
constexpr uint32_t kRingBlocks      = 8;           // ring capacity in blocks

enum ServerState : uint32_t {
    ServerAbsent   = 0,
    ServerStarting = 1,
    ServerRunning  = 2,
    ServerError    = 3,
};

#pragma pack(push, 8)
struct ShmHeader {
    uint32_t magic = kMagic;
    uint32_t version = kProtocolVersion;
    std::atomic<uint32_t> sampleRate{48000};
    std::atomic<uint32_t> channels{1};
    std::atomic<uint32_t> blockFrames{kDefaultBlock};
    uint32_t ringCapacityFrames = kRingBlocks * kDefaultBlock;
    std::atomic<uint32_t> resyncCount{0};

    std::atomic<uint64_t> inputRead{0};   // server side
    std::atomic<uint64_t> inputWrite{0};  // APO side
    std::atomic<uint64_t> outputRead{0};  // APO side
    std::atomic<uint64_t> outputWrite{0}; // server side

    std::atomic<uint32_t> serverState{ServerAbsent};
    std::atomic<uint32_t> pluginLatencyUs{0};
    std::atomic<uint32_t> activePluginCount{0};
    std::atomic<uint32_t> configGeneration{0};
    std::atomic<uint32_t> framesProcessed{0};
    std::atomic<float>    meterInPeak{0.0f};
    std::atomic<float>    meterOutPeak{0.0f};
};
#pragma pack(pop)

inline const char* server_state_name(ServerState s)
{
    switch (s) {
        case ServerAbsent:   return "absent";
        case ServerStarting: return "starting";
        case ServerRunning:  return "running";
        case ServerError:    return "error";
    }
    return "unknown";
}

inline std::wstring shm_name(const std::wstring& endpointSafeId)
{
    return L"Local\\STGR_" + endpointSafeId;
}
inline std::wstring event_name(const std::wstring& endpointSafeId)
{
    return L"Local\\STGR_Evt_" + endpointSafeId;
}

// Byte offsets of the section regions (see layout above).
struct SectionLayout {
    uint32_t header;
    uint32_t seqIn;
    uint32_t dataIn;
    uint32_t seqOut;
    uint32_t dataOut;
    uint32_t total;
};

inline SectionLayout layout(uint32_t channels, uint32_t capacityFrames)
{
    SectionLayout l{};
    l.header = 0;
    l.seqIn  = (uint32_t)sizeof(ShmHeader);
    l.dataIn = l.seqIn + capacityFrames * (uint32_t)sizeof(uint64_t);
    l.seqOut = l.dataIn + capacityFrames * channels * (uint32_t)sizeof(float);
    l.dataOut = l.seqOut + capacityFrames * (uint32_t)sizeof(uint64_t);
    l.total  = l.dataOut + capacityFrames * channels * (uint32_t)sizeof(float);
    // Align the total to 8 bytes.
    l.total = (l.total + 7) & ~7u;
    return l;
}

// Total mapping size for a section. The layout always reserves space for
// kMaxChannels so both sides can remap independently of the current channel
// count (which may change when the endpoint format changes).
inline uint32_t section_bytes()
{
    return layout(kMaxChannels, kRingBlocks * kDefaultBlock).total;
}

// A single SPSC frame ring (interleaved float data).
class FrameRing {
public:
    FrameRing() = default;

    void init(float* data, uint32_t capacityFrames, uint32_t channels,
              std::atomic<uint64_t>* writeCounter, std::atomic<uint64_t>* readCounter)
    {
        data_ = data;
        capacity_ = capacityFrames;
        channels_ = channels;
        write_ = writeCounter;
        read_ = readCounter;
    }

    uint32_t available() const
    {
        const uint64_t w = write_->load(std::memory_order_acquire);
        const uint64_t r = read_->load(std::memory_order_acquire);
        return w >= r ? (uint32_t)(w - r) : 0u;
    }

    uint32_t free() const
    {
        const uint32_t a = available();
        return a >= capacity_ ? 0u : capacity_ - a;
    }

    // Producer side. Returns false when the ring is full.
    bool push(const float* src, uint32_t frames)
    {
        const uint64_t w = write_->load(std::memory_order_relaxed);
        const uint64_t r = read_->load(std::memory_order_acquire);
        if (w - r + frames > capacity_) return false;
        write_at(w, src, frames);
        write_->store(w + frames, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when not enough data.
    bool pull(float* dst, uint32_t frames)
    {
        const uint64_t r = read_->load(std::memory_order_relaxed);
        const uint64_t w = write_->load(std::memory_order_acquire);
        if (w - r < frames) return false;
        read_at(r, dst, frames);
        read_->store(r + frames, std::memory_order_release);
        return true;
    }

    void reset()
    {
        write_->store(0, std::memory_order_relaxed);
        read_->store(0, std::memory_order_release);
    }

private:
    void write_at(uint64_t index, const float* src, uint32_t frames)
    {
        const uint32_t n = channels_ * frames;
        const uint32_t start = (uint32_t)(index % capacity_) * channels_;
        const uint32_t total = capacity_ * channels_;
        const uint32_t first = (start + n <= total) ? n : (total - start);
        memcpy(data_ + start, src, first * sizeof(float));
        if (first < n) memcpy(data_, src + first, (n - first) * sizeof(float));
    }

    void read_at(uint64_t index, float* dst, uint32_t frames)
    {
        const uint32_t n = channels_ * frames;
        const uint32_t start = (uint32_t)(index % capacity_) * channels_;
        const uint32_t total = capacity_ * channels_;
        const uint32_t first = (start + n <= total) ? n : (total - start);
        memcpy(dst, data_ + start, first * sizeof(float));
        if (first < n) memcpy(dst + first, data_, (n - first) * sizeof(float));
    }

    float* data_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t channels_ = 0;
    std::atomic<uint64_t>* write_ = nullptr;
    std::atomic<uint64_t>* read_ = nullptr;
};

// SPSC ring for per-frame sequence tags (uint64).
class SeqRing {
public:
    SeqRing() = default;

    void init(std::atomic<uint64_t>* data, uint32_t capacityFrames,
              std::atomic<uint64_t>* writeCounter, std::atomic<uint64_t>* readCounter)
    {
        data_ = data;
        capacity_ = capacityFrames;
        write_ = writeCounter;
        read_ = readCounter;
    }

    bool push(uint64_t value, uint32_t frames)
    {
        const uint64_t w = write_->load(std::memory_order_relaxed);
        const uint64_t r = read_->load(std::memory_order_acquire);
        if (w - r + frames > capacity_) return false;
        const uint32_t start = (uint32_t)(w % capacity_);
        for (uint32_t i = 0; i < frames; ++i)
            data_[(start + i) % capacity_].store(value, std::memory_order_relaxed);
        write_->store(w + frames, std::memory_order_release);
        return true;
    }

    uint64_t head() const
    {
        const uint64_t r = read_->load(std::memory_order_acquire);
        return data_[r % capacity_].load(std::memory_order_acquire);
    }

    // Consume 'frames' sequence values (call after the matching data pull).
    void consume(uint32_t frames)
    {
        const uint64_t r = read_->load(std::memory_order_relaxed);
        read_->store(r + frames, std::memory_order_release);
    }

    void reset()
    {
        write_->store(0, std::memory_order_relaxed);
        read_->store(0, std::memory_order_release);
    }

private:
    std::atomic<uint64_t>* data_ = nullptr;
    uint32_t capacity_ = 0;
    std::atomic<uint64_t>* write_ = nullptr;
    std::atomic<uint64_t>* read_ = nullptr;
};

// RAII wrapper around a named shared-memory section.
class SharedSection {
public:
    SharedSection() = default;
    ~SharedSection() { close(); }

    bool open(const std::wstring& name, bool create, uint32_t bytes, DWORD access)
    {
        if (create) {
            hMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, bytes, name.c_str());
            if (!hMap_ && GetLastError() == ERROR_ALREADY_EXISTS)
                hMap_ = OpenFileMappingW(access, FALSE, name.c_str());
        } else {
            hMap_ = OpenFileMappingW(access, FALSE, name.c_str());
        }
        if (!hMap_) return false;
        view_ = MapViewOfFile(hMap_, access, 0, 0, bytes);
        if (!view_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
            return false;
        }
        return true;
    }

    void* view() const { return view_; }
    bool valid() const { return view_ != nullptr; }

    void close()
    {
        if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
        if (hMap_) { CloseHandle(hMap_); hMap_ = nullptr; }
    }

private:
    HANDLE hMap_ = nullptr;
    void* view_ = nullptr;
};

} // namespace stgr::bridge
