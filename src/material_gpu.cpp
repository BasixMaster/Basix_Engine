// SPDX-License-Identifier: MIT
// Paired CPU-double / GPU-float evaluation and PDF conformance, not a renderer.
#include "basix/material.hpp"
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using basix::render::Vec3;
std::atomic<unsigned> errors{0},warnings{0};
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
 VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;
 else if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)++warnings;
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
  std::cerr<<"Basix validation: "<<(data&&data->pMessage?data->pMessage:"missing diagnostic")<<'\n';
 return VK_FALSE;
}
struct Device {
 gpu::Device* device=nullptr;gpu::CommandPool* pool=nullptr;gpu::TimelineSemaphore* timeline=nullptr;
 VkInstance instance=nullptr;VkDebugUtilsMessengerEXT messenger=nullptr;
 PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger=nullptr;bool validation=false;
 ~Device(){if(device){gpu::wait_idle(device);if(pool)gpu::destroy_command_pool(pool);
  if(timeline)gpu::destroy_timeline_semaphore(timeline);if(messenger)destroy_messenger(instance,messenger,nullptr);gpu::destroy_device(device);}}
 void initialize(bool require_validation){
#ifdef NDEBUG
  require(!require_validation,"--validate requires Debug; refusing an unvalidated Release run");
#endif
  gpu::set_error_callback([](const char* message,bool)noexcept{std::cerr<<"NoGraphicsAPI: "<<message<<'\n';});
  auto init=gpu::create_device({});device=init.device;require(device,"NoGraphicsAPI material device creation failed");
  auto native=gpu::basix_native_vulkan(device);instance=static_cast<VkInstance>(native.instance);validation=native.validation_enabled;
  require(!require_validation||validation,"Requested Khronos validation is unavailable");
  if(validation){
   auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
   destroy_messenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
   require(create&&destroy_messenger,"Validation debug-utils entry points missing");
   VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
   info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
   info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
   info.pfnUserCallback=diagnostic;require(create(instance,&info,nullptr,&messenger)==VK_SUCCESS,"Validation messenger creation failed");
  }
  pool=gpu::create_command_pool(device);timeline=gpu::create_timeline_semaphore(device);require(pool&&timeline,"GPU command resources unavailable");
 }
};
// Array elements use C-layout scalar fields, not a compiler-dependent float3.
struct alignas(16) MaterialCase {
 float base_r,base_g,base_b,roughness;
 float wo_x,wo_y,wo_z,metallic;
 float wi_x,wi_y,wi_z;std::uint32_t model;
};
struct alignas(16) MaterialResult {float r,g,b,density;};
struct alignas(16) Root {std::uint64_t cases,results;std::uint32_t count,pad0,pad1,pad2;};
static_assert(sizeof(float)==4&&sizeof(std::uint32_t)==4);
static_assert(sizeof(MaterialCase)==48&&alignof(MaterialCase)==16);
static_assert(offsetof(MaterialCase,roughness)==12&&offsetof(MaterialCase,wo_x)==16);
static_assert(offsetof(MaterialCase,metallic)==28&&offsetof(MaterialCase,wi_x)==32&&offsetof(MaterialCase,model)==44);
static_assert(sizeof(MaterialResult)==16&&offsetof(MaterialResult,density)==12);
static_assert(sizeof(Root)==32&&offsetof(Root,results)==8&&offsetof(Root,count)==16);

MaterialCase make_case(std::uint32_t model,Vec3 base,double rough,double metal,Vec3 wo,Vec3 wi){
 return {float(base.x),float(base.y),float(base.z),float(rough),float(wo.x),float(wo.y),float(wo.z),float(metal),
         float(wi.x),float(wi.y),float(wi.z),model};
}
Vec3 hemisphere(double z,double phi){double radius=std::sqrt(1-z*z);return {radius*std::cos(phi),radius*std::sin(phi),z};}
struct Random {
 std::uint32_t value=0x97a8c021;
 double next(){value=value*1664525u+1013904223u;return double(value>>8)*0x1.0p-24;}
};
std::vector<MaterialCase> cases(){
 std::vector<MaterialCase> out;out.reserve(2048);
 // Exactly specified landmarks exercise analytic Lambert and normal-incidence
 // microfacet peaks before the broader deterministic input distribution.
 out.push_back(make_case(0,{.2,.4,.8},.5,0,{0,0,1},{0,0,1}));
 out.push_back(make_case(0,{0,0,0},.05,0,{0,0,1},hemisphere(.01,.3)));
 out.push_back(make_case(0,{1,1,1},1,0,hemisphere(.2,.4),hemisphere(.9,.8)));
 for(double rough:{.05,.2,.5,1.})for(double metal:{0.,1.})
  out.push_back(make_case(1,{.4,.7,.9},rough,metal,{0,0,1},{0,0,1}));
 for(double z:{1e-6,1e-4,.01,.1})for(double rough:{.05,.3,1.})
  out.push_back(make_case(1,{.9,.5,.1},rough,.5,hemisphere(z,0),hemisphere(z,std::numbers::pi)));
 Random random;while(out.size()<2048){
  const auto i=out.size();const std::uint32_t model=i%5==0?0:1;
  Vec3 base{random.next(),random.next(),random.next()};
  double rough=i%9==0?.05:.05+.95*random.next();double metal=i%3==0?0:(i%3==1?1:random.next());
  double zo=i%13==0?1e-5:.001+.999*random.next();double zi=i%11==0?1e-5:.001+.999*random.next();
  out.push_back(make_case(model,base,rough,metal,hemisphere(zo,2*std::numbers::pi*random.next()),hemisphere(zi,2*std::numbers::pi*random.next())));
 }
 return out;
}
std::array<double,4> oracle(const MaterialCase& c){
 using namespace basix::render;
 // Promote the ACTUAL uploaded floats, rather than comparing against seed data
 // before float quantization. Unit direction tolerance is the CPU API contract.
 Material m{1,{c.base_r,c.base_g,c.base_b},c.roughness,c.metallic,{},c.model?MaterialModel::metallic_roughness:MaterialModel::lambert};
 Vec3 wo{c.wo_x,c.wo_y,c.wo_z},wi{c.wi_x,c.wi_y,c.wi_z};require(validate(m),"Generated material invalid");
 auto f=evaluate(m,wo,wi);return {f.x,f.y,f.z,pdf(m,wo,wi)};
}
std::vector<std::uint32_t> read_shader(){
 std::ifstream file(std::filesystem::path(BASIX_SHADER_DIR)/"material_conformance.spv",std::ios::binary|std::ios::ate);
 require(bool(file),"Material conformance shader missing");auto length=file.tellg();require(length>0&&length%4==0,"Invalid material shader bytes");
 std::vector<std::uint32_t> result(static_cast<size_t>(length)/4);file.seekg(0);file.read(reinterpret_cast<char*>(result.data()),length);require(bool(file),"Material shader read failed");return result;
}
struct Measurement {unsigned cases{},components{},failed{},lambert{},ggx{};double max_abs{},max_rel{},max_scaled{};};
Measurement evaluate(Device& owner){
 const auto input=cases();const size_t input_offset=sizeof(Root),output_offset=input_offset+input.size()*sizeof(MaterialCase);
 struct Resources {gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(heap.owner)gpu::destroy_gpu_heap(heap);}} r;
 r.heap=gpu::create_gpu_heap(owner.device,output_offset+input.size()*sizeof(MaterialResult));
 require(r.heap.owner&&r.heap.range.cpu&&r.heap.range.gpu,"Mapped material GPU heap unavailable");
 auto* root=reinterpret_cast<Root*>(r.heap.range.cpu);
 const auto address=reinterpret_cast<std::uint64_t>(r.heap.range.gpu);
 *root={address+input_offset,address+output_offset,static_cast<std::uint32_t>(input.size()),0,0,0};
 std::memcpy(r.heap.range.cpu+input_offset,input.data(),input.size()*sizeof(MaterialCase));
 auto* output=reinterpret_cast<MaterialResult*>(r.heap.range.cpu+output_offset);
 const float nan=std::numeric_limits<float>::quiet_NaN();
 for(size_t i=0;i<input.size();++i)output[i]={nan,nan,nan,nan};
 const auto code=read_shader();r.pso=gpu::create_compute_pso(owner.device,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={64,1,1}});
 require(r.pso,"Material pipeline creation failed");
 auto* commands=gpu::begin_commands(owner.pool);gpu::bind_pso(commands,r.pso);gpu::dispatch(commands,r.heap.range.gpu,{32,1,1});
 VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};memory.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
 memory.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;memory.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;memory.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;
 VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&memory;
 vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands)),&dependency);
 gpu::end_commands(commands);const gpu::TimelinePoint done{.semaphore=owner.timeline,.value=1};
 gpu::submit(owner.device,{.commands={commands},.completion=done});gpu::wait_timeline(done);
 Measurement result;result.cases=static_cast<unsigned>(input.size());
 constexpr double absolute=3e-4,relative=1e-3;
 for(size_t i=0;i<input.size();++i){const auto reference=oracle(input[i]);
  if(input[i].model)++result.ggx;else ++result.lambert;
  const double actual[]={output[i].r,output[i].g,output[i].b,output[i].density};
  for(unsigned channel=0;channel<4;++channel){++result.components;const double error=std::abs(actual[channel]-reference[channel]);
   const double tolerance=absolute+relative*std::abs(reference[channel]);
   const bool valid=std::isfinite(actual[channel])&&actual[channel]>=0&&std::isfinite(reference[channel])&&reference[channel]>=0&&error<=tolerance;
   if(!valid){if(result.failed<8)std::cerr<<"Case "<<i<<" channel "<<channel<<" actual="<<actual[channel]<<" CPU="<<reference[channel]<<" tolerance="<<tolerance<<'\n';++result.failed;}
   if(std::isfinite(error)){result.max_abs=std::max(result.max_abs,error);result.max_rel=std::max(result.max_rel,error/std::max(1e-20,std::abs(reference[channel])));result.max_scaled=std::max(result.max_scaled,error/tolerance);}
  }
 }
 return result;
}
} // namespace
int main(int argc,char** argv){
 try{
  std::filesystem::path output="material-gpu-output";bool validate=false;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--output"&&i+1<argc)output=argv[++i];else if(arg=="--validate")validate=true;else throw std::runtime_error("Usage: basix_material_gpu [--output DIRECTORY] [--validate]");}
#ifdef NDEBUG
  require(!validate,"--validate requires Debug; refusing an unvalidated Release run");
#endif
  std::filesystem::create_directories(output);Measurement measured;bool validation=false;
  {Device owner;owner.initialize(validate);validation=owner.validation;measured=evaluate(owner);}
  const bool pass=measured.failed==0&&errors==0&&warnings==0;
  std::ofstream receipt(output/"result.json");receipt<<std::setprecision(12)
   <<"{\"status\":\""<<(pass?"passed":"failed")<<"\",\"cases\":"<<measured.cases<<",\"components\":"<<measured.components
   <<",\"lambert_cases\":"<<measured.lambert<<",\"ggx_cases\":"<<measured.ggx<<",\"failed_components\":"<<measured.failed
   <<",\"absolute_tolerance\":0.0003,\"relative_tolerance\":0.001,\"max_absolute_error\":"<<measured.max_abs<<",\"max_relative_error\":"<<measured.max_rel<<",\"max_error_over_tolerance\":"<<measured.max_scaled
   <<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<errors<<",\"validation_warnings\":"<<warnings
   <<",\"scope\":\"Paired CPU-double/GPU-float BRDF and directional-PDF conformance on exact uploaded floats; Lambert and single-scatter isotropic GGX. Independent physics checks are CPU-only. No GPU sampling, material textures, renderer image quality or cross-device claim.\"}\n";
  require(bool(receipt),"Material conformance receipt write failed");require(pass,"Material GPU conformance or validation failed");
  std::cout<<"PASS "<<measured.cases<<" GPU material cases / "<<measured.components<<" components; max tolerance fraction="<<measured.max_scaled<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
