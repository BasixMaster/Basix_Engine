// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <limits>
#include <optional>

namespace basix {

// CPU reference math only. These double-precision types are not a GPU ABI.
struct Vec3 { double x{}, y{}, z{}; };
constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
constexpr Vec3 operator*(double s, Vec3 a) { return a*s; }
constexpr Vec3 operator/(Vec3 a, double s) { return {a.x/s,a.y/s,a.z/s}; }
constexpr double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}

using Tetrahedron = std::array<Vec3,4>;
using Barycentric = std::array<double,4>;
struct Ray {
    Vec3 origin{}, direction{};
    double t_min{0.0}, t_max{std::numeric_limits<double>::infinity()};
};
struct RayInterval { double enter{}, exit{}; };
struct Aabb { Vec3 minimum{}, maximum{}; };
enum class CageError { none, invalid_tolerance, non_finite, degenerate_rest,
                       degenerate_deformed, inverted_deformation };

// A single, orientation-preserving piecewise-affine cell. This is a correctness
// prototype, not a cage generator, watertight traversal algorithm or GPU BLAS.
// Rest/deformed tet vertex correspondence must be identical. Cells with normalized
// determinant <= relative_tolerance are rejected, regardless of world-unit scale.
class CageMap {
public:
    static std::optional<CageMap> create(const Tetrahedron& rest,
                                        const Tetrahedron& deformed,
                                        CageError* error = nullptr,
                                        double relative_tolerance = 1e-12);

    [[nodiscard]] Barycentric rest_barycentric(Vec3 point) const;
    [[nodiscard]] Barycentric deformed_barycentric(Vec3 point) const;
    // Point/direction inputs must be finite. Directions are deliberately never
    // normalized: affine ray transforms preserve the original parameter t.
    [[nodiscard]] Vec3 to_rest(Vec3 point) const;
    [[nodiscard]] Vec3 to_deformed(Vec3 point) const;
    [[nodiscard]] Ray ray_to_rest(const Ray& ray) const;
    [[nodiscard]] Ray ray_to_deformed(const Ray& ray) const;
    // Inverse-transpose normal transform; returns an unnormalized normal.
    [[nodiscard]] Vec3 normal_to_deformed(Vec3 rest_normal) const;
    [[nodiscard]] Aabb deformed_bounds() const;
    // Conservative cell clip, t >= max(0,t_min), with dimensionless barycentric
    // slack. Shared faces are included by both cells; callers deduplicate hits.
    // A zero-direction ray inside the cell retains its supplied interval.
    [[nodiscard]] std::optional<RayInterval> clip_deformed(
        const Ray& ray, double barycentric_slack = 64.0 * std::numeric_limits<double>::epsilon()) const;

private:
    using Matrix = std::array<Vec3,3>; // columns
    struct Frame { Vec3 anchor; Matrix edges, normalized_inverse; double scale; };
    CageMap() = default;
    static std::optional<Frame> frame(const Tetrahedron&, double tolerance, double& determinant);
    static Barycentric barycentric(const Frame&,Vec3);
    static Vec3 from_barycentric(const Frame&,const Barycentric&);
    static Vec3 direction_between(const Frame& from,const Frame& to,Vec3);
    Frame rest_{}, deformed_{};
    Tetrahedron deformed_vertices_{};
};

} // namespace basix
