#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace cp2 {

// ZGP 1.807 RVA 0x1d130/0x1d190: the DLL's double constant is
// float-precision PI promoted to double, not a full-precision PI constant.
constexpr double zgp_pi = 3.1415927410125732421875;

inline std::int32_t zgp_truncate(double value) {
    // MSVCRT _ftol returns a 64-bit integer, while these wrappers keep EAX.
    // Its integer-indefinite value is INT64_MIN, whose low DWORD is zero.
    std::int64_t wide = std::numeric_limits<std::int64_t>::min();
    if (std::isfinite(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0)
        wide = static_cast<std::int64_t>(value);
    const auto bits = static_cast<std::uint32_t>(wide);
    std::int32_t narrow;
    std::memcpy(&narrow, &bits, sizeof(narrow));
    return narrow;
}

inline double zgp_radians(std::int32_t angle, std::int32_t turn) {
    if (!turn) turn = 256;
    return ((static_cast<double>(angle) * 360.0 / turn) * zgp_pi) / 180.0;
}

inline std::int32_t zgp_sin(std::int32_t angle, std::int32_t amplitude, std::int32_t turn) {
    return zgp_truncate(std::sin(zgp_radians(angle, turn)) * amplitude);
}

inline std::int32_t zgp_cos(std::int32_t angle, std::int32_t amplitude, std::int32_t turn) {
    return zgp_truncate(std::cos(zgp_radians(angle, turn)) * amplitude);
}

inline void zgp_scos(std::int32_t* output, std::int32_t angle,
                     std::int32_t amplitude, std::int32_t turn) {
    if (!output) throw std::invalid_argument("ZGP z_scos requires two output integers");
    output[0] = zgp_sin(angle, amplitude, turn);
    output[1] = zgp_cos(angle, amplitude, turn);
}

// ZGP RVA 0x1d6a0: shortest distance and signed direction from first to second.
// Half-turn ties retain +1/-1 based on the normalized input ordering.
inline void zgp_adif(std::int32_t* output, std::int32_t first,
                     std::int32_t second, std::int32_t turn) {
    if (!output) throw std::invalid_argument("ZGP z_adif requires two output integers");
    if (!turn) turn = 256;
    if (turn < 0) throw std::invalid_argument("ZGP angle period must be positive");
    first %= turn;
    second %= turn;
    if (first < 0) first += turn;
    if (second < 0) second += turn;
    std::int32_t direction = 1;
    if (first > second) {
        const auto temporary = first;
        first = second;
        second = temporary;
        direction = -1;
    }
    auto distance = second - first;
    if (distance > turn / 2) {
        distance = turn - distance;
        direction = -direction;
    }
    output[0] = distance;
    output[1] = distance ? direction : 0;
}

inline std::int32_t zgp_signed_bits(std::uint32_t bits) {
    std::int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline std::int32_t zgp_negate(std::int32_t value) {
    return zgp_signed_bits(0u - static_cast<std::uint32_t>(value));
}

// Return the raw export result. The old HSP extension convention negates this
// negative numerical return when exposing it as stat; callers must retain that.
inline std::int32_t zgp_len(std::int32_t x1, std::int32_t y1,
                           std::int32_t x2, std::int32_t y2) {
    const auto x = static_cast<std::uint32_t>(x1) - static_cast<std::uint32_t>(x2);
    const auto y = static_cast<std::uint32_t>(y1) - static_cast<std::uint32_t>(y2);
    // Original z_len deliberately computes square/sum in wrapping DWORDs.
    const auto square = zgp_signed_bits(x * x + y * y);
    return zgp_negate(zgp_truncate(std::sqrt(static_cast<double>(square))));
}

inline std::int32_t zgp_len3(const std::int32_t* origin, std::int32_t x,
                            std::int32_t y, std::int32_t z) {
    if (!origin) throw std::invalid_argument("ZGP z_len3 requires three input integers");
    const double dx = zgp_signed_bits(static_cast<std::uint32_t>(x) - static_cast<std::uint32_t>(origin[0]));
    const double dy = zgp_signed_bits(static_cast<std::uint32_t>(y) - static_cast<std::uint32_t>(origin[1]));
    const double dz = zgp_signed_bits(static_cast<std::uint32_t>(z) - static_cast<std::uint32_t>(origin[2]));
    return zgp_negate(zgp_truncate(std::sqrt(dx * dx + dy * dy + dz * dz)));
}

inline std::int32_t zgp_dir(std::int32_t x1, std::int32_t y1,
                           std::int32_t x2, std::int32_t y2,
                           float angle_period = 256.0f) {
    // x87 does these subtractions in floating point, avoiding DWORD overflow.
    const double dx = static_cast<double>(x2) - x1;
    const double dy = static_cast<double>(y1) - y2;
    float degrees = static_cast<float>(std::atan2(dx, dy) * 360.0 / (2.0 * zgp_pi));
    if (degrees < 0.0f) degrees += 360.0f;
    return zgp_negate(zgp_truncate(static_cast<double>(degrees) * angle_period / 360.0));
}

} // namespace cp2
