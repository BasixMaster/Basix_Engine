// SPDX-License-Identifier: MIT
#pragma once
#include "basix/material.hpp"

namespace basix::render {
// Distinct isotropic GGX visible-normal method, not the legacy NDF mapping.
// Accepted near-unit directions are normalized before this method evaluates.
// A sample retains its generated half-vector: reconstructing it from a rounded
// wi is a separate diagnostic and is not the sampled-state transport contract.
struct VisibleSample {
    Vec3 wi{}, f{};
    double pdf{};
    bool valid{};
    Vec3 half{};
    double projection{};
};
Vec3 evaluate_visible(const Material&, Vec3 wo, Vec3 wi) noexcept;
double pdf_visible(const Material&, Vec3 wo, Vec3 wi) noexcept;
VisibleSample sample_visible(const Material&, Vec3 wo,
                             double u_lobe, double u1, double u2) noexcept;
} // namespace basix::render
