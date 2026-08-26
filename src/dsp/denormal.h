// Denormal (subnormal) handling helpers.
// On x86/x64, flushing denormals to zero avoids massive CPU slowdowns in
// recursive filters and envelope followers.
#pragma once
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#include <xmmintrin.h>
#endif

namespace stgr::dsp {

// Enable FTZ + DAZ on the current thread (SSE). No-op where unsupported.
inline void enable_flush_to_zero()
{
#ifdef _MSC_VER
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ | DAZ
#endif
}

// Returns true when the sample is subnormal (for tests and diagnostics).
inline bool is_denormal(float v)
{
    const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(&v);
    return (bits & 0x7F800000u) == 0u && (bits & 0x007FFFFFu) != 0u;
}

inline bool is_finite(float v)
{
    const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(&v);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Snapshot of the SSE control register (restored by RAII on destruction).
class ScopedFtz {
public:
#ifdef _MSC_VER
    ScopedFtz() : old_(_mm_getcsr()) { _mm_setcsr(old_ | 0x8040); }
    ~ScopedFtz() { _mm_setcsr(old_); }
private:
    unsigned int old_;
#else
    ScopedFtz() {}
#endif
};

} // namespace stgr::dsp
