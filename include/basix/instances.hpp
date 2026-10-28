// SPDX-License-Identifier: MIT
#pragma once
#include "basix/reference.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace basix::instances {
using Matrix = std::array<float, 12>;
struct InputInstance {
    std::uint32_t stable_id{}, primitive_id_base{}, material_override{};
    Matrix matrix{1,0,0,0, 0,1,0,0, 0,0,1,0};
};
// Dense TLAS custom index selects this record; stable_id is independent of that
// slot and of primitive_id_base. Scalar ABI is also usable by the GPU adapter.
struct Metadata {
    std::uint32_t first_triangle{}, triangle_count{}, stable_id{}, epoch{};
};
static_assert(sizeof(Metadata) == 16);
struct Batch {
    // Shader vertex order always corresponds to the unchanged object BLAS.
    reference::Scene shader_scene;
    // The reference copy keeps the same vertex/sampling order and carries an
    // explicit authored-normal orientation flag. It never swaps the vertices.
    // Shader normals use orientation_per_triangle; shader_scene flags are false.
    reference::Scene reference_scene;
    std::vector<Metadata> metadata;
    std::vector<float> orientation_per_triangle;
    std::vector<Matrix> matrices;
    std::vector<double> condition_numbers;
};
inline constexpr double maximum_condition_number = 1e6;
// Validates a bounded FP32 mesh/transform data model, not a hardware traversal
// error theorem. All object and output positions are quantized to FP32. Throws
// std::invalid_argument before returning any partial result on invalid input.
Batch bake_instances(const reference::Scene& mesh,
                     std::span<const InputInstance> instances,
                     std::uint32_t epoch);
} // namespace basix::instances
