// SPDX-License-Identifier: MIT
#include "basix/reference.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using basix::render::Vec3;
template<class T> T integer(const std::string& text) {
    T value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc()||result.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid integer: "+text);
    return value;
}
double number(const std::string& text) {
    double value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc()||result.ptr!=text.data()+text.size()||!std::isfinite(value))throw std::invalid_argument("Invalid finite number: "+text);
    return value;
}
double preview(double linear) {
    const double x=std::max(0.,linear)/(1+std::max(0.,linear));
    return x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;
}
void write_float(std::ofstream& file,double value) {
    if(!std::isfinite(value)||value<0||value>std::numeric_limits<float>::max())throw std::runtime_error("HDR float export out of range");
    const auto bits=std::bit_cast<std::uint32_t>(static_cast<float>(value));
    const char bytes[]={char(bits&255),char((bits>>8)&255),char((bits>>16)&255),char((bits>>24)&255)};file.write(bytes,4);
}
std::uint64_t checksum(const std::vector<Vec3>& image) {
    std::uint64_t hash=14695981039346656037ULL;
    for(auto p:image)for(double value:{p.x,p.y,p.z}) {
        const auto bits=std::bit_cast<std::uint64_t>(value);
        for(unsigned i=0;i<8;++i){hash^=(bits>>(8*i))&255;hash*=1099511628211ULL;}
    }
    return hash;
}
}
int main(int argc,char** argv) {
    try {
        using namespace basix::reference;
        RenderConfig config;Camera camera;std::uint32_t spp=32,batch=8;std::string scene="cornell";std::filesystem::path out="reference-output";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto next=[&](){if(i+1>=argc)throw std::invalid_argument("Missing value for "+arg);return std::string(argv[++i]);};
            if(arg=="--width")config.width=integer<std::uint32_t>(next());
            else if(arg=="--height")config.height=integer<std::uint32_t>(next());
            else if(arg=="--spp")spp=integer<std::uint32_t>(next());
            else if(arg=="--batch")batch=integer<std::uint32_t>(next());
            else if(arg=="--seed")config.seed=integer<std::uint64_t>(next());
            else if(arg=="--depth")config.integrator.max_depth=integer<std::uint32_t>(next());
            else if(arg=="--rr-start")config.integrator.rr_start=integer<std::uint32_t>(next());
            else if(arg=="--random-bits")config.integrator.random_bits=integer<std::uint32_t>(next());
            else if(arg=="--fov")camera.vertical_fov_degrees=number(next());
            else if(arg=="--scene")scene=next();
            else if(arg=="--output")out=next();
            else if(arg=="--no-nee")config.integrator.next_event_estimation=false;
            else if(arg=="--no-mis")config.integrator.multiple_importance_sampling=false;
            else if(arg=="--no-rr")config.integrator.russian_roulette=false;
            else throw std::invalid_argument("Unknown argument. Options: --scene cornell --width N --height N --spp N --batch N --seed N --depth N --rr-start N --random-bits 24|53 --fov DEGREES --output DIRECTORY --no-nee --no-mis --no-rr");
        }
        if(scene!="cornell")throw std::invalid_argument("Unknown reference scene ID");
        if(spp<1||spp>1000000||batch<1||batch>1000000)throw std::invalid_argument("SPP and batch must be from 1 to 1000000");
        if(std::uint64_t(config.width)*config.height*spp*(std::uint64_t(config.integrator.max_depth)+1)>100000000)throw std::invalid_argument("Reference workload exceeds 100M worst-case scattering/segment budget");
        ProgressiveRenderer renderer(cornell_scene(),camera,config);
        const auto started=std::chrono::steady_clock::now();
        for(std::uint32_t remaining=spp;remaining;) { const auto count=std::min(batch,remaining);renderer.add_samples(count);remaining-=count; }
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        const auto image=renderer.linear_image();std::filesystem::create_directories(out);
        std::ofstream ppm(out/"preview.ppm",std::ios::binary),pfm(out/"linear.pfm",std::ios::binary);
        ppm<<"P6\n"<<config.width<<' '<<config.height<<"\n255\n";
        for(auto p:image)for(double value:{p.x,p.y,p.z})ppm.put(char(std::lround(std::clamp(preview(value),0.,1.)*255)));
        pfm<<"PF\n"<<config.width<<' '<<config.height<<"\n-1.0\n";
        for(std::uint32_t row=config.height;row>0;--row)for(std::uint32_t x=0;x<config.width;++x) {
            const auto p=image[std::size_t(row-1)*config.width+x];for(double value:{p.x,p.y,p.z})write_float(pfm,value);
        }
        ppm.close();pfm.close();if(!ppm||!pfm)throw std::runtime_error("Image output write failed");
        double mean=0;for(auto p:image)mean+=basix::render::luminance(p);mean/=image.size();
        std::ofstream receipt(out/"result.json");receipt<<std::setprecision(17)
            <<"{\"status\":\"passed\",\"scene\":\"cornell\",\"width\":"<<config.width<<",\"height\":"<<config.height
            <<",\"samples_per_pixel\":"<<renderer.samples_per_pixel()<<",\"seed\":"<<config.seed<<",\"batch\":"<<batch
            <<",\"random_bits\":"<<config.integrator.random_bits
            <<",\"max_scattering_events\":"<<config.integrator.max_depth<<",\"rr_start\":"<<config.integrator.rr_start<<",\"vertical_fov_degrees\":"<<camera.vertical_fov_degrees
            <<",\"next_event_estimation\":"<<(config.integrator.next_event_estimation?"true":"false")
            <<",\"power_heuristic_mis\":"<<(config.integrator.multiple_importance_sampling?"true":"false")
            <<",\"russian_roulette\":"<<(config.integrator.russian_roulette?"true":"false")
            <<",\"render_elapsed_ms\":"<<elapsed<<",\"mean_linear_luminance\":"<<mean
            <<",\"linear_double_fnv1a64\":\""<<std::hex<<checksum(image)<<std::dec
            <<"\",\"linear_output\":\"linear.pfm; RGB float32 little-endian, bottom-up rows\",\"preview\":\"preview.ppm; Reinhard then sRGB transfer\",\"scope\":\"Original CPU progressive reference PT, analytic spheres/triangles, one-sided triangle emitters, NEE/MIS/RR. No GPU PT, importer, textures, motion guides or UE-parity acceptance.\"}\n";
        receipt.close();if(!receipt)throw std::runtime_error("Reference receipt write failed");
        std::cout<<"PASS CPU reference: "<<config.width<<'x'<<config.height<<", "<<spp<<" spp, mean linear luminance "<<mean<<", "<<elapsed<<" ms\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
