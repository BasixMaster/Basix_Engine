// SPDX-License-Identifier: MIT
#include "basix/material.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

using namespace basix::render;
namespace {
constexpr double pi=std::numbers::pi;
int checks=0,failures=0;
void check(bool value,const char* label) {
    ++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}
}
bool near(double a,double b,double eps=1e-9) { return std::abs(a-b)<=eps; }
bool near(Vec3 a,Vec3 b,double eps=1e-9) { return max_component({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)})<=eps; }
Vec3 hemi(double z,double phi) {const double r=std::sqrt(1-z*z);return {r*std::cos(phi),r*std::sin(phi),z};}
struct Random {
    std::mt19937_64 engine{0x8914783};
    double next(){return double(engine()>>11)*0x1.0p-53;}
};
// Independent midpoint integration over solid angle: dOmega = dz dPhi.
template<class Function> double quadrature(Function f,int nz=320,int np=640) {
    double sum=0;for(int z=0;z<nz;++z)for(int p=0;p<np;++p)
        sum+=f(hemi((z+.5)/nz,2*pi*(p+.5)/np));
    return sum*2*pi/(nz*np);
}
void lambert() {
    Material m{1,{.2,.4,.8},.5,0,{},MaterialModel::lambert};
    check(validate(m),"valid Lambert material");
    check(near(evaluate(m,{0,0,1},normalize({1,2,3})),m.base_color/pi),"analytic Lambert BRDF");
    check(near(quadrature([&](Vec3 wi){return pdf(m,{0,0,1},wi);}),1,1e-10),"Lambert density normalizes");
    Random random;bool transport=true;
    for(int i=0;i<10000;++i){auto s=sample(m,{0,0,1},random.next(),random.next(),random.next());
        transport&=s.valid&&near(s.f*(s.wi.z/s.pdf),m.base_color,2e-14);}
    check(transport,"Lambert sampled white-environment radiance equals albedo");
}
void reciprocity_and_energy() {
    Material m{2,{1,1,1},.5,0,{},MaterialModel::metallic_roughness};Random r;bool reciprocal=true;
    for(int i=0;i<12000;++i){auto wo=hemi(.001+.999*r.next(),2*pi*r.next()),wi=hemi(.001+.999*r.next(),2*pi*r.next());
        reciprocal&=near(evaluate(m,wo,wi),evaluate(m,wi,wo),1e-10);}
    check(reciprocal,"GGX diffuse mixture obeys Helmholtz reciprocity");
    // Hemispherical white-environment reflectance is tested independently of
    // sample()/pdf(), including dielectric, mixed and conducting endpoints.
    for(double rough:{.3,.6,1.})for(double metal:{0.,.5,1.})for(double z:{1.,.6,.1}) {
        m.roughness=rough;m.metallic=metal;const auto wo=hemi(z,0);
        const double energy=quadrature([&](Vec3 wi){return evaluate(m,wo,wi).x*wi.z;});
        if(!(std::isfinite(energy)&&energy>=0&&energy<=1.003))
            std::cerr<<"energy rough="<<rough<<" metal="<<metal<<" z="<<z<<" value="<<energy<<'\n';
        check(std::isfinite(energy)&&energy>=0&&energy<=1.003,"hemispherical reflected energy stays bounded");
    }
}
void ndf_null_mass() {
    Material m{3,{.8,.6,.2},1,1,{},MaterialModel::metallic_roughness};
    // At normal incidence and alpha=1, the projected NDF is cosine-weighted.
    // Reflection is above the horizon iff normal.z > sqrt(1/2), giving exact
    // acceptance mass 1/2. The reflected valid directions have uniform density.
    const double integrated=quadrature([&](Vec3 wi){return pdf(m,{0,0,1},wi);});
    check(near(integrated,.5,2e-12),"rough conductor continuous PDF mass is analytically one half");
    Random r;unsigned valid=0;bool finite_samples=true;
    for(unsigned i=0;i<200000;++i){auto s=sample(m,{0,0,1},r.next(),r.next(),r.next());
        if(s.valid){++valid;finite_samples&=finite(s.f)&&finite(s.wi)&&s.pdf>0;}}
    check(std::abs(double(valid)/200000-.5)<.004,"NDF null-event frequency matches analytic mass");
    check(finite_samples,"valid NDF samples have finite BRDF and density");
}
void distribution_agreement() {
    Material m{4,{.7,.4,.2},.7,.3,{},MaterialModel::metallic_roughness};
    const Vec3 wo=hemi(.35,.7);constexpr unsigned count=300000;constexpr int bins=12;
    std::array<unsigned,bins> observed{};unsigned null=0;Random r;
    // Bin events by equal-solid-angle z intervals. Expected masses use only the
    // evaluator's directional PDF; observed samples include the full null mass.
    for(unsigned i=0;i<count;++i){auto s=sample(m,wo,r.next(),r.next(),r.next());
        if(!s.valid){++null;continue;}
        observed[std::min(bins-1,int(s.wi.z*bins))]++;
    }
    std::array<double,bins> expected{};
    constexpr int nz=960,np=640;double mass=0;
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){const double height=(z+.5)/nz;
        const double value=pdf(m,wo,hemi(height,2*pi*(p+.5)/np))*2*pi/(nz*np);
        expected[std::min(bins-1,int(height*bins))]+=value;mass+=value;}
    bool agreement=true;for(int b=0;b<bins;++b)
        agreement&=std::abs(double(observed[b])/count-expected[b])<.003;
    check(agreement,"GGX mixture sample histogram matches independent PDF quadrature");
    check(std::abs(double(null)/count-(1-mass))<.004,"off-normal mixture null mass agrees with PDF complement");
}
void invalid_and_grazing() {
    Material m{5,{.4,.6,.8},.05,0,{},MaterialModel::metallic_roughness};
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    auto bad=m;bad.id=0;check(!validate(bad),"zero material ID rejected");
    for(double x:{-.1,1.1,nan,inf}){bad=m;bad.base_color.x=x;check(!validate(bad),"invalid color rejected");}
    for(double x:{.049,1.01,nan,inf}){bad=m;bad.roughness=x;check(!validate(bad),"invalid roughness rejected");}
    for(double x:{-.1,1.01,nan,inf}){bad=m;bad.metallic=x;check(!validate(bad),"invalid metallic rejected");}
    for(double x:{-.1,nan,inf}){bad=m;bad.emission.y=x;check(!validate(bad),"invalid emission rejected");}
    bad=m;bad.model=static_cast<MaterialModel>(99);check(!validate(bad),"unknown material model rejected");
    for(Vec3 wo:std::array<Vec3,5>{{{0,0,0},{0,0,-1},{0,0,2},{nan,0,1},{1,0,0}}}) {
        check(!sample(m,wo,.2,.3,.4).valid,"invalid outgoing direction rejects sampling");
        check(pdf(m,wo,{0,0,1})==0&&near(evaluate(m,wo,{0,0,1}),{}),"invalid direction yields zero density/BRDF");
    }
    for(double u:{-.1,1.,nan,inf})check(!sample(m,{0,0,1},u,.2,.3).valid,"invalid uniform variate rejected");
    bool grazing=true;for(double rough:{.05,.2,1.})for(double z:{1e-12,1e-8,1e-4,.1,1.}) {
        m.roughness=rough;const auto wo=hemi(z,0),wi=hemi(z,pi);
        grazing&=finite(evaluate(m,wo,wi))&&std::isfinite(pdf(m,wo,wi));
        auto s=sample(m,wo,0,.1,.3);grazing&=!s.valid||(finite(s.f)&&std::isfinite(s.pdf));
    }
    check(grazing,"minimum roughness and near-horizon evaluations remain finite");
    bad=m;bad.emission={1e20,0,3};check(validate(bad),"finite HDR emission is accepted independently of albedo");
}
} // namespace
int main(){lambert();reciprocity_and_energy();ndf_null_mass();distribution_agreement();invalid_and_grazing();
    std::cout<<checks<<" material checks; "<<failures<<" failures\n";return failures?1:0;}
