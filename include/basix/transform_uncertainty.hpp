// SPDX-License-Identifier: MIT
#pragma once
#include "basix/instances.hpp"

namespace basix::transform_uncertainty {
struct Domain { double origin_limit=32, max_distance=128; };
struct Budget {
    render::Vec3 representation{}, forward_policy{}, object_policy{}, inverse_policy{}, total{};
};
inline constexpr double maximum_condition_number=1024;
inline constexpr double maximum_magnitude=0x1p20;
// Input mesh positions must already be exactly uploaded FP32 values. Results
// follow shader_scene triangle order; each component is upward-rounded FP32,
// represented as double. Throws invalid_argument without partial results.
// Representation encloses exact-real vertex representation error. The other
// three terms are conditional FP32 arithmetic/intersector policies, not a
// universal hardware traversal proof. The world reconstruction helper must
// still add its own error; total is EXTRA uncertainty supplied to that helper.
// gamma7 terms assume normal finite FP32 intermediate operations, or exact
// zero; rejecting subnormal inputs alone does not establish that assumption.
// The altitude/64 rule caps each extra-error component, not final displacement
// or separation between distinct nearby triangles.
// Domain values must be exactly FP32, with max_distance >= 4*origin_limit.
// Ray direction components
// must be bounded by one. Object extent policy is per-axis 2*max(|e1|,|e2|).
// A certified upper condition bound must be <=1024; outward arithmetic can
// conservatively refuse exact boundary cases.
std::vector<Budget> analyze(const reference::Scene& uploaded_mesh,
                           const instances::Batch&, Domain domain={});
} // namespace basix::transform_uncertainty
