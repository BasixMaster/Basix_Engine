// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace basix::render {
// Double precision CPU reference values; this is not a shader/GPU ABI.
struct Vec3 { double x{}, y{}, z{}; };
constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
constexpr Vec3 operator-(Vec3 a) { return {-a.x,-a.y,-a.z}; }
constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x*b.x,a.y*b.y,a.z*b.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
constexpr Vec3 operator*(double s, Vec3 a) { return a*s; }
constexpr Vec3 operator/(Vec3 a, double s) { return {a.x/s,a.y/s,a.z/s}; }
constexpr double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline Vec3 normalize(Vec3 a) {
    const double length=std::hypot(a.x,a.y,a.z);
    return finite(a)&&length>0&&std::isfinite(length) ? a/length : Vec3{};
}
constexpr double max_component(Vec3 a) { return std::max({a.x,a.y,a.z}); }
constexpr double luminance(Vec3 a) { return .2126*a.x+.7152*a.y+.0722*a.z; }

enum class MaterialModel { lambert, metallic_roughness };
struct Material {
    std::uint32_t id{};
    Vec3 base_color{.5,.5,.5};
    double roughness{.5};
    double metallic{};
    Vec3 emission{};
    MaterialModel model{MaterialModel::lambert};
};
struct BsdfSample { Vec3 wi{}, f{}; double pdf{}; bool valid{}; };

bool validate(const Material& material) noexcept;
// Reflection only, local normal +Z. wo and wi point away from the surface and
// must be unit directions in the upper hemisphere. Invalid inputs return zero.
Vec3 evaluate(const Material& material, Vec3 wo, Vec3 wi) noexcept;
double pdf(const Material& material, Vec3 wo, Vec3 wi) noexcept;
// Uniform variates must lie in [0,1). Ordinary GGX NDF sampling includes null
// events: below-horizon/reflected-back-facing samples return valid=false. They
// must NOT be resampled; the continuous pdf integrates to the non-null mass.
BsdfSample sample(const Material& material, Vec3 wo,
                  double u_lobe, double u1, double u2) noexcept;
} // namespace basix::render
