// SPDX-License-Identifier: MIT
#pragma once

namespace basix::ray_spawn {
// Float data model for the bounded Vulkan triangle experiment, not world doubles.
struct Float3 { float x{}, y{}, z{}; };
struct Point {
    Float3 position{}, error{}, normal{};
    bool valid{};
};
struct Spawn { Float3 origin{}; bool valid{}; };

// Reconstruct a point on the uploaded FP32 triangle. The reconstruction envelope
// is separate from traversal_error: a nonnegative caller-provided absolute box.
// There is no universal Vulkan hardware traversal error bound in this API.
// Normals are derived from the geometric edges; no shading normal is accepted.
Point triangle_point(Float3 a, Float3 b, Float3 c, float u, float v,
                     Float3 traversal_error = {}) noexcept;
// Project the box along the oriented geometric normal, then round each moved
// component outward. Invalid/overflowing data fails closed. A valid return does
// not establish arbitrary hardware, transforms, subnormal or scene-scale safety.
Spawn offset(const Point& point, Float3 outgoing) noexcept;
} // namespace basix::ray_spawn
