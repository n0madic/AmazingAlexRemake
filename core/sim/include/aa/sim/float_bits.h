// Exact float32 constants. Some literals of the original binary are not the shortest decimal one would
// write (e.g. the eight-ball radius 0x3d67e0f0), so they are carried as bit patterns read from the binary.
#pragma once

#include <cstdint>
#include <cstring>

namespace aa::sim {

inline float floatFromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

inline std::uint32_t bitsFromFloat(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

}  // namespace aa::sim
