// SPDX-License-Identifier: MIT
#pragma once
#include "basix/material.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace basix::reference {
using render::Vec3;
struct Ray { Vec3 origin{}, direction{}; double t_min=0, t_max=std::numeric_limits<double>::infinity(); };
struct Sphere { std::uint32_t id{},material_id{}; Vec3 center{}; double radius{}; };
struct Triangle { std::uint32_t id{},material_id{}; Vec3 a{},b{},c{}; };
struct Scene { std::vector<render::Material> materials; std::vector<Sphere> spheres; std::vector<Triangle> triangles; Vec3 environment{}; };
struct Hit { double t{}; Vec3 position{},normal{},geometric_normal{}; std::uint32_t primitive_id{},material_id{}; bool front_face{}; };
struct LightSample { Vec3 position{},normal{},emission{},wi{}; double distance{},pdf_area{},pdf_solid_angle{}; std::uint32_t primitive_id{}; bool valid{}; };
struct Camera { Vec3 origin{0,0,3.5},target{0,-.1,0},up{0,1,0}; double vertical_fov_degrees=40; };
enum class SamplingProposal { ndf_legacy, visible_v1 };
struct IntegratorSettings {
    // Maximum scattering events; the final segment still sees emission/environment.
    std::uint32_t max_depth=8,rr_start=3;
    bool next_event_estimation=true,multiple_importance_sampling=true,russian_roulette=true;
    // Top 24 bits permit an exactly representable [0,1) GPU-float stream.
    // Default 53 preserves the original CPU reference sequence.
    std::uint32_t random_bits=53;
    SamplingProposal sampling_proposal=SamplingProposal::ndf_legacy;
};
struct RenderConfig { std::uint32_t width=64,height=64; std::uint64_t seed=1; IntegratorSettings integrator{}; };

bool validate(const Scene&,std::string* reason=nullptr);
bool validate(const Camera&);
bool validate(const IntegratorSettings&);
double power_heuristic(double a,double b);

// Immutable validated scene snapshot. Brute-force geometry, no acceleration or
// GPU layout. Triangle emission is one-sided; emitting spheres are rejected.
class World {
public:
    explicit World(Scene scene);
    [[nodiscard]] const Scene& scene() const noexcept { return scene_; }
    [[nodiscard]] const render::Material& material(std::uint32_t id) const;
    [[nodiscard]] std::optional<Hit> intersect(const Ray&) const;
    [[nodiscard]] LightSample sample_light(Vec3 reference,double u_select,double u1,double u2) const;
    [[nodiscard]] double light_pdf(Vec3 reference,const Hit& light_hit) const;
    [[nodiscard]] double light_area() const noexcept { return total_light_area_; }
private:
    struct Light { Triangle triangle; Vec3 normal; double area; };
    Scene scene_;std::vector<Light> lights_;double total_light_area_{};
};

// Light-sample contribution including visibility and optional MIS. The hit's
// normal is the oriented shading normal and wo points away from the surface.
Vec3 direct_lighting(const World&,const Hit&,Vec3 wo,const LightSample&,bool use_mis,SamplingProposal proposal=SamplingProposal::ndf_legacy);
// Deterministic sample-key based path; no scheduling/global RNG state.
Vec3 radiance(const World&,const Ray&,const IntegratorSettings&,std::uint64_t sample_key);
Scene cornell_scene();

class ProgressiveRenderer {
public:
    ProgressiveRenderer(Scene scene,Camera camera,RenderConfig config);
    void add_samples(std::uint32_t samples_per_pixel);
    [[nodiscard]] std::vector<Vec3> linear_image() const;
    [[nodiscard]] std::uint64_t samples_per_pixel() const noexcept { return samples_; }
    [[nodiscard]] const RenderConfig& config() const noexcept { return config_; }
private:
    World world_;Camera camera_;RenderConfig config_;std::vector<Vec3> sums_;std::uint64_t samples_{};
};
} // namespace basix::reference
