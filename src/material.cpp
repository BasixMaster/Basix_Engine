// SPDX-License-Identifier: MIT
#include "basix/material.hpp"
#include <numbers>

namespace basix::render {
namespace {
constexpr double pi=std::numbers::pi;
constexpr Vec3 one{1,1,1};
bool direction(Vec3 v) { return finite(v)&&v.z>0&&std::abs(dot(v,v)-1)<1e-6; }
bool unit_interval(double x) { return std::isfinite(x)&&x>=0&&x<1; }
Vec3 fresnel(Vec3 f0,double cosine) {
    const double t=1-std::clamp(cosine,0.,1.);
    const double fifth=t*t*t*t*t;
    return f0+(one-f0)*fifth;
}
double distribution(double alpha,Vec3 h) {
    const double a2=alpha*alpha;
    const double t=h.x*h.x+h.y*h.y+a2*h.z*h.z;
    return a2/(pi*t*t);
}
double smith_denominator(double alpha,double z) {
    return z+std::sqrt(alpha*alpha+(1-alpha*alpha)*z*z);
}
double specular_probability(const Material& m) { return .5+.5*m.metallic; }
} // namespace

bool validate(const Material& m) noexcept {
    return m.id!=0&&finite(m.base_color)&&m.base_color.x>=0&&m.base_color.y>=0&&
        m.base_color.z>=0&&max_component(m.base_color)<=1&&std::isfinite(m.roughness)&&
        m.roughness>=.05&&m.roughness<=1&&std::isfinite(m.metallic)&&m.metallic>=0&&
        m.metallic<=1&&finite(m.emission)&&m.emission.x>=0&&m.emission.y>=0&&
        m.emission.z>=0&&(m.model==MaterialModel::lambert||m.model==MaterialModel::metallic_roughness);
}

Vec3 evaluate(const Material& m,Vec3 wo,Vec3 wi) noexcept {
    if(!validate(m)||!direction(wo)||!direction(wi))return {};
    if(m.model==MaterialModel::lambert)return m.base_color/pi;
    const Vec3 h=normalize(wo+wi);
    const double alpha=m.roughness*m.roughness;
    const Vec3 f0=Vec3{.04,.04,.04}*(1-m.metallic)+m.base_color*m.metallic;
    // Original scalar implementation of isotropic GGX, separable Smith masking
    // and Schlick Fresnel. Algebra cancels cosines before evaluation to remain
    // finite at grazing angles. Multiple-scattering compensation is not added.
    const Vec3 spec=fresnel(f0,dot(wo,h))*(distribution(alpha,h)/
        (smith_denominator(alpha,wo.z)*smith_denominator(alpha,wi.z)));
    // A conservative reciprocal dielectric diffuse layer. Endpoint Fresnel
    // attenuation is explicit; this is not claimed to implement glTF exactly.
    const Vec3 dielectric{.04,.04,.04};
    const Vec3 diff=m.base_color*((1-m.metallic)/pi)*
        (one-fresnel(dielectric,wo.z))*(one-fresnel(dielectric,wi.z));
    return diff+spec;
}

double pdf(const Material& m,Vec3 wo,Vec3 wi) noexcept {
    if(!validate(m)||!direction(wo)||!direction(wi))return 0;
    const double diffuse=wi.z/pi;
    if(m.model==MaterialModel::lambert)return diffuse;
    const Vec3 h=normalize(wo+wi);
    const double projection=dot(wo,h);
    if(projection<=0)return 0;
    const double spec=distribution(m.roughness*m.roughness,h)*h.z/(4*projection);
    const double p=specular_probability(m);
    return p*spec+(1-p)*diffuse;
}

BsdfSample sample(const Material& m,Vec3 wo,double u_lobe,double u1,double u2) noexcept {
    if(!validate(m)||!direction(wo)||!unit_interval(u_lobe)||!unit_interval(u1)||!unit_interval(u2))return {};
    Vec3 wi;
    const double phi=2*pi*u2;
    if(m.model==MaterialModel::lambert||u_lobe>=specular_probability(m)) {
        const double radius=std::sqrt(u1);
        wi={radius*std::cos(phi),radius*std::sin(phi),std::sqrt(1-u1)};
    } else {
        const double alpha=m.roughness*m.roughness;
        // Inversion of the projected GGX NDF CDF; this is NDF, NOT VNDF sampling.
        const double tan2=alpha*alpha*u1/(1-u1);
        const double z=1/std::sqrt(1+tan2),r=std::sqrt(tan2)*z;
        const Vec3 h{r*std::cos(phi),r*std::sin(phi),z};
        const double projection=dot(wo,h);
        if(projection<=0)return {};
        wi=h*(2*projection)-wo;
        if(wi.z<=0)return {};
        wi=normalize(wi);
    }
    const double density=pdf(m,wo,wi);
    const Vec3 f=evaluate(m,wo,wi);
    return {wi,f,density,density>0&&std::isfinite(density)&&finite(f)};
}
} // namespace basix::render
