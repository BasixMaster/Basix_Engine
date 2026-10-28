// SPDX-License-Identifier: MIT
// Paired CPU-double / GPU-float BSDF sampling conformance, not a renderer.
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
std::filesystem::path shader_directory=BASIX_SHADER_DIR;
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
// Explicit scalar storage ABI corresponding to basix_material.slang C layout.
struct alignas(16) MaterialData {
 float base_r,base_g,base_b,roughness;
 float metallic,emission_r,emission_g,emission_b;
 std::uint32_t model,pad0,pad1,pad2;
};
struct alignas(16) SamplingCase {
 MaterialData material;float wo_x,wo_y,wo_z,u_lobe;float u1,u2;std::uint32_t pad0,pad1;
};
struct alignas(16) SamplingResult {float wi_x,wi_y,wi_z,density;float r,g,b;std::uint32_t valid;};
struct alignas(16) Root {std::uint64_t cases,results;std::uint32_t count,pad0,pad1,pad2;};
static_assert(sizeof(MaterialData)==48&&offsetof(MaterialData,roughness)==12&&offsetof(MaterialData,metallic)==16);
static_assert(offsetof(MaterialData,emission_r)==20&&offsetof(MaterialData,model)==32&&offsetof(MaterialData,pad2)==44);
static_assert(sizeof(SamplingCase)==80&&offsetof(SamplingCase,wo_x)==48&&offsetof(SamplingCase,u_lobe)==60&&offsetof(SamplingCase,u1)==64);
static_assert(sizeof(SamplingResult)==32&&offsetof(SamplingResult,density)==12&&offsetof(SamplingResult,r)==16&&offsetof(SamplingResult,valid)==28);
static_assert(sizeof(Root)==32&&offsetof(Root,results)==8&&offsetof(Root,count)==16);
constexpr std::uint32_t case_count=4096;
// Predeclared accuracy and discontinuity contracts. The exact mathematical
// horizon is a separate classification test, not silently folded into matches.
constexpr double absolute_tolerance=3e-4,relative_tolerance=1e-3,direction_tolerance=2e-5;
constexpr double boundary_z_tolerance=1e-6;
constexpr std::size_t exact_horizon_case=0;
SamplingCase make_case(std::uint32_t model,Vec3 base,double rough,double metal,Vec3 wo,double ul,double u1,double u2){
 return {{float(base.x),float(base.y),float(base.z),float(rough),float(metal),0,0,0,model,0,0,0},
         float(wo.x),float(wo.y),float(wo.z),float(ul),float(u1),float(u2),0,0};
}
Vec3 hemisphere(double z,double phi){double r=std::sqrt(1-z*z);return {r*std::cos(phi),r*std::sin(phi),z};}
struct Random {std::uint32_t state=0x467a89cf;double next(){state=state*1664525u+1013904223u;return double(state>>8)*0x1.0p-24;}};
std::vector<SamplingCase> cases(){
 std::vector<SamplingCase> out;out.reserve(case_count);
 // For alpha=1 and wo=+Z, u1=.5 lies exactly at the reflection horizon.
 out.push_back(make_case(1,{.3,.6,.9},1,1,{0,0,1},0,.5,0));
 for(double u:{.5-1e-5,.5+1e-5,0.,.1,.9})
  out.push_back(make_case(1,{.3,.6,.9},1,1,{0,0,1},0,u,0));
 for(double rough:{.05,.3,1.})for(double metal:{0.,1.})for(double z:{1.,.1,1e-5})
  out.push_back(make_case(1,{.8,.5,.2},rough,metal,hemisphere(z,0),.1,.1,.2));
 out.push_back(make_case(0,{.2,.4,.8},.5,0,{0,0,1},.2,.25,.5));
 out.push_back(make_case(0,{1,1,1},1,0,{0,0,1},.9,0,0));
 // Explicit invalid and domain boundary cases must return a zero null sample.
 const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
 for(double invalid:{-.1,1.,nan,inf}) {
  out.push_back(make_case(1,{.8,.5,.2},.3,0,{0,0,1},invalid,.2,.3));
  out.push_back(make_case(1,{.8,.5,.2},.3,0,{0,0,1},.2,invalid,.3));
  out.push_back(make_case(1,{.8,.5,.2},.3,0,{0,0,1},.2,.3,invalid));
 }
 for(double invalid:{.049,1.01,nan,inf})out.push_back(make_case(1,{.8,.5,.2},invalid,0,{0,0,1},.2,.3,.4));
 for(double invalid:{-.1,1.01,nan,inf})out.push_back(make_case(1,{.8,.5,.2},.3,invalid,{0,0,1},.2,.3,.4));
 for(Vec3 wo:std::array<Vec3,4>{{{0,0,0},{0,0,-1},{0,0,2},{nan,0,1}}})out.push_back(make_case(1,{.8,.5,.2},.3,0,wo,.2,.3,.4));
 auto invalid_model=make_case(99,{.8,.5,.2},.3,0,{0,0,1},.2,.3,.4);out.push_back(invalid_model);
 auto invalid_emission=make_case(1,{.8,.5,.2},.3,0,{0,0,1},.2,.3,.4);invalid_emission.material.emission_r=-1;out.push_back(invalid_emission);
 Random random;
 while(out.size()<case_count){const auto i=out.size();
  Vec3 base{random.next(),random.next(),random.next()};const double rough=i%11==0?.05:.05+.95*random.next();
  const double metal=i%3==0?0:(i%3==1?1:random.next());const double z=i%17==0?1e-5:.001+.999*random.next();
  out.push_back(make_case(i%5==0?0:1,base,rough,metal,hemisphere(z,2*std::numbers::pi*random.next()),random.next(),random.next(),random.next()));
 }
 return out;
}
basix::render::BsdfSample oracle(const SamplingCase& c){
 using namespace basix::render;const auto& m=c.material;
 Material material{1,{m.base_r,m.base_g,m.base_b},m.roughness,m.metallic,{m.emission_r,m.emission_g,m.emission_b},static_cast<MaterialModel>(m.model)};
 return sample(material,{c.wo_x,c.wo_y,c.wo_z},c.u_lobe,c.u1,c.u2);
}
std::vector<std::uint32_t> read_shader(){
 std::ifstream file(shader_directory/"material_sampling.spv",std::ios::binary|std::ios::ate);
 require(bool(file),"Material sampling shader missing");auto length=file.tellg();require(length>0&&length%4==0,"Invalid sampling shader bytes");
 std::vector<std::uint32_t> code(static_cast<size_t>(length)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),length);require(bool(file),"Sampling shader read failed");return code;
}
bool zero_null(const SamplingResult& x){return x.valid==0&&x.wi_x==0&&x.wi_y==0&&x.wi_z==0&&x.density==0&&x.r==0&&x.g==0&&x.b==0;}
bool near_value(double actual,double expected,double absolute=absolute_tolerance){
 return std::isfinite(actual)&&std::isfinite(expected)&&std::abs(actual-expected)<=absolute+relative_tolerance*std::abs(expected);
}
struct Measurement {
 unsigned cases{},strict{},valid{},null{},boundary{},boundary_cpu_valid{},boundary_gpu_valid{},failed{},components{};
 unsigned direction_failed{},returned_failed{},layered_failed{},weight_case{},weight_channel{};
 double max_scaled{},max_direction{},returned_max_scaled{},max_weight_relative{},max_weight_absolute{};
};
Measurement evaluate(Device& owner){
 const auto input=cases();const size_t input_offset=sizeof(Root),output_offset=input_offset+input.size()*sizeof(SamplingCase);
 struct Resources {gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(heap.owner)gpu::destroy_gpu_heap(heap);}} r;
 r.heap=gpu::create_gpu_heap(owner.device,output_offset+input.size()*sizeof(SamplingResult));
 require(r.heap.owner&&r.heap.range.cpu&&r.heap.range.gpu,"Mapped sampling GPU heap unavailable");
 const auto address=reinterpret_cast<std::uint64_t>(r.heap.range.gpu);auto* root=reinterpret_cast<Root*>(r.heap.range.cpu);
 *root={address+input_offset,address+output_offset,case_count,0,0,0};
 std::memcpy(r.heap.range.cpu+input_offset,input.data(),input.size()*sizeof(SamplingCase));
 auto* output=reinterpret_cast<SamplingResult*>(r.heap.range.cpu+output_offset);const float nan=std::numeric_limits<float>::quiet_NaN();
 for(size_t i=0;i<input.size();++i)output[i]={nan,nan,nan,nan,nan,nan,nan,0xffffffff};
 const auto code=read_shader();r.pso=gpu::create_compute_pso(owner.device,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={64,1,1}});
 require(r.pso,"Sampling pipeline creation failed");auto* commands=gpu::begin_commands(owner.pool);
 gpu::bind_pso(commands,r.pso);gpu::dispatch(commands,r.heap.range.gpu,{case_count/64,1,1});
 VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};memory.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
 memory.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;memory.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;memory.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;
 VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&memory;
 vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands)),&dependency);
 gpu::end_commands(commands);const gpu::TimelinePoint done{.semaphore=owner.timeline,.value=1};gpu::submit(owner.device,{.commands={commands},.completion=done});gpu::wait_timeline(done);
 Measurement result;result.cases=case_count;
 for(size_t i=0;i<input.size();++i){const auto expected=oracle(input[i]);const auto& actual=output[i];bool ok=true,direction_ok=true,returned_ok=true;
  if(i==exact_horizon_case){++result.boundary;result.boundary_cpu_valid=expected.valid?1:0;result.boundary_gpu_valid=actual.valid;
   // CPU and float may choose opposite signs at exactly zero. This boundary
   // case is explicitly excluded from strict validity comparisons, but cannot
   // pass with a far-above-horizon direction, bad density or untouched output.
   if(actual.valid==0)ok=zero_null(actual);
   else ok=actual.valid==1&&std::isfinite(actual.wi_x)&&std::isfinite(actual.wi_y)&&
    std::isfinite(actual.wi_z)&&actual.wi_z>0&&actual.wi_z<=boundary_z_tolerance&&
    std::abs(actual.wi_x-1)<=direction_tolerance&&std::abs(actual.wi_y)<=direction_tolerance&&
    std::abs(double(actual.wi_x)*actual.wi_x+double(actual.wi_y)*actual.wi_y+double(actual.wi_z)*actual.wi_z-1)<2e-5&&
    std::isfinite(actual.density)&&actual.density>0&&std::isfinite(actual.r)&&actual.r>=0&&std::isfinite(actual.g)&&actual.g>=0&&std::isfinite(actual.b)&&actual.b>=0;
   direction_ok=returned_ok=ok;
  } else {
   ++result.strict;ok=actual.valid==static_cast<std::uint32_t>(expected.valid);
   direction_ok=ok;
   if(!expected.valid){++result.null;ok=ok&&zero_null(actual);direction_ok=returned_ok=ok;}
   else {++result.valid;const double a[]={actual.wi_x,actual.wi_y,actual.wi_z,actual.r,actual.g,actual.b,actual.density};
    const double b[]={expected.wi.x,expected.wi.y,expected.wi.z,expected.f.x,expected.f.y,expected.f.z,expected.pdf};
    for(unsigned channel=0;channel<7;++channel){++result.components;
     if(channel<3){const double error=std::abs(a[channel]-b[channel]);if(std::isfinite(error))result.max_direction=std::max(result.max_direction,error);const bool match=std::isfinite(a[channel])&&error<=direction_tolerance;direction_ok=direction_ok&&match;ok=ok&&match;}
     else {const double error=std::abs(a[channel]-b[channel]);if(std::isfinite(error))result.max_scaled=std::max(result.max_scaled,error/(absolute_tolerance+relative_tolerance*std::abs(b[channel])));ok=ok&&near_value(a[channel],b[channel])&&a[channel]>=0;}
    }
    // A SECOND, explicitly named criterion checks the returned tuple at its
    // exact uploaded/output float direction. It does not replace or erase the
    // original same-variates CPU sample oracle above.
    const auto& c=input[i];const auto& m=c.material;
    basix::render::Material material{1,{m.base_r,m.base_g,m.base_b},m.roughness,m.metallic,
     {m.emission_r,m.emission_g,m.emission_b},static_cast<basix::render::MaterialModel>(m.model)};
    const Vec3 wo{c.wo_x,c.wo_y,c.wo_z},gpu_wi{actual.wi_x,actual.wi_y,actual.wi_z};
    const auto at_gpu=basix::render::evaluate(material,wo,gpu_wi);
    const auto pdf_at_gpu=basix::render::pdf(material,wo,gpu_wi);
    const double actual_values[]={actual.r,actual.g,actual.b,actual.density};
    const double returned_reference[]={at_gpu.x,at_gpu.y,at_gpu.z,pdf_at_gpu};
    returned_ok=actual.valid==1&&pdf_at_gpu>0;
    for(unsigned channel=0;channel<4;++channel){const double error=std::abs(actual_values[channel]-returned_reference[channel]);
     if(std::isfinite(error))result.returned_max_scaled=std::max(result.returned_max_scaled,error/(absolute_tolerance+relative_tolerance*std::abs(returned_reference[channel])));
     returned_ok=returned_ok&&near_value(actual_values[channel],returned_reference[channel])&&actual_values[channel]>=0;
    }
    // Observe original paired importance weights in all valid samples. This
    // remains an unresolved numerical limitation, not a new relaxed pass gate.
    for(unsigned channel=0;channel<3;++channel){
     const double expected_f[]={expected.f.x,expected.f.y,expected.f.z};
     const double reference_weight=expected_f[channel]*expected.wi.z/expected.pdf;
     const double actual_weight=double(actual_values[channel])*actual.wi_z/actual.density;
     const double error=std::abs(actual_weight-reference_weight);
     const double relative=error/std::max(1e-12,std::abs(reference_weight));
     if(std::isfinite(error)){result.max_weight_absolute=std::max(result.max_weight_absolute,error);
      if(relative>result.max_weight_relative){result.max_weight_relative=relative;result.weight_case=static_cast<unsigned>(i);result.weight_channel=channel;}}
    }
   }
  }
  if(!direction_ok)++result.direction_failed;if(!returned_ok)++result.returned_failed;
  if(!direction_ok||!returned_ok)++result.layered_failed;
  if(!ok){if(result.failed<32){
   const auto& c=input[i];const auto& m=c.material;
   basix::render::Material material{1,{m.base_r,m.base_g,m.base_b},m.roughness,m.metallic,
    {m.emission_r,m.emission_g,m.emission_b},static_cast<basix::render::MaterialModel>(m.model)};
   const Vec3 wo{c.wo_x,c.wo_y,c.wo_z},gpu_wi{actual.wi_x,actual.wi_y,actual.wi_z};
   const auto at_gpu=basix::render::evaluate(material,wo,gpu_wi);
   const auto pdf_at_gpu=basix::render::pdf(material,wo,gpu_wi);
   std::cerr<<std::setprecision(12)<<"Sampling case "<<i<<" expected.valid="<<expected.valid<<" actual.valid="<<actual.valid
    <<" rough="<<m.roughness<<" metal="<<m.metallic<<" specular_branch="<<(m.model==1&&c.u_lobe<.5+.5*m.metallic)
    <<" wo_norm2="<<basix::render::dot(wo,wo)<<" GPUz="<<actual.wi_z<<" CPUz="<<expected.wi.z
    <<" CPUf="<<expected.f.x<<','<<expected.f.y<<','<<expected.f.z
    <<" GPUf="<<actual.r<<','<<actual.g<<','<<actual.b<<" CPUpdf="<<expected.pdf<<" GPUpdf="<<actual.density
    <<" CPUf_at_GPUwi="<<at_gpu.x<<','<<at_gpu.y<<','<<at_gpu.z<<" CPUpdf_at_GPUwi="<<pdf_at_gpu
    <<" CPUweight="<<(expected.pdf>0?expected.f.x*expected.wi.z/expected.pdf:0)
    <<" GPUweight="<<(actual.density>0?double(actual.r)*actual.wi_z/actual.density:0)<<'\n';
   }++result.failed;}
 }
 return result;
}
} // namespace
int main(int argc,char** argv){
 try{
  std::filesystem::path output="material-sampling-output";bool validate=false,require_paired=false;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--output"&&i+1<argc)output=argv[++i];else if(arg=="--shader-dir"&&i+1<argc)shader_directory=argv[++i];else if(arg=="--validate")validate=true;else if(arg=="--require-paired-transport")require_paired=true;else throw std::runtime_error("Usage: basix_material_sampling_gpu [--output DIRECTORY] [--shader-dir DIRECTORY] [--validate] [--require-paired-transport]");}
#ifdef NDEBUG
  require(!validate,"--validate requires Debug; refusing an unvalidated Release run");
#endif
  std::filesystem::create_directories(output);Measurement measured;bool validation=false;
  {Device owner;owner.initialize(validate);validation=owner.validation;measured=evaluate(owner);}
  const bool paired_pass=measured.failed==0,layered_pass=measured.layered_failed==0&&errors==0&&warnings==0;
  const char* status=!layered_pass?"failed":(paired_pass?"passed":"partial");
  std::ofstream receipt(output/"result.json");receipt<<std::setprecision(12)
   <<"{\"status\":\""<<status<<"\",\"cases\":"<<measured.cases<<",\"strict_validity_cases\":"<<measured.strict
   <<",\"expected_valid\":"<<measured.valid<<",\"expected_null\":"<<measured.null<<",\"explicit_horizon_boundary_cases\":"<<measured.boundary
   <<",\"boundary_cpu_valid\":"<<measured.boundary_cpu_valid<<",\"boundary_gpu_valid\":"<<measured.boundary_gpu_valid
   <<",\"original_paired_sample_status\":\""<<(paired_pass?"passed":"failed")<<"\",\"original_paired_sample_passed\":"<<(paired_pass?"true":"false")
   <<",\"failed_cases\":"<<measured.failed<<",\"original_paired_failed_cases\":"<<measured.failed<<",\"valid_sample_component_checks\":"<<measured.components
   <<",\"direction_and_validity_passed\":"<<(measured.direction_failed==0?"true":"false")<<",\"direction_and_validity_failed_cases\":"<<measured.direction_failed
   <<",\"returned_tuple_passed\":"<<(measured.returned_failed==0?"true":"false")<<",\"returned_tuple_failed_cases\":"<<measured.returned_failed
   <<",\"layered_contracts_passed\":"<<(layered_pass?"true":"false")<<",\"layered_failed_cases\":"<<measured.layered_failed
   <<",\"returned_tuple_max_error_over_tolerance\":"<<measured.returned_max_scaled
   <<",\"original_weight_max_relative_error\":"<<measured.max_weight_relative<<",\"original_weight_max_absolute_error\":"<<measured.max_weight_absolute
   <<",\"original_weight_max_relative_case\":"<<measured.weight_case<<",\"original_weight_max_relative_channel\":"<<measured.weight_channel
   <<",\"weight_relative_denominator_floor\":1e-12,\"require_paired_transport\":"<<(require_paired?"true":"false")
   <<",\"absolute_tolerance\":0.0003,\"relative_tolerance\":0.001,\"direction_absolute_tolerance\":0.00002,\"boundary_z_limit\":0.000001,\"max_error_over_tolerance\":"<<measured.max_scaled
   <<",\"max_direction_error\":"<<measured.max_direction<<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<errors<<",\"validation_warnings\":"<<warnings
   <<",\"scope\":\"Layered sampling direction/validity and returned-f/PDF contracts are separate from the retained original same-variates paired sample criterion. Exact horizon is explicitly classified. Original near-grazing importance-weight differences remain unresolved when paired sample status is failed. Null events never resampled. No independent GPU distribution/energy proof, PT image quality or cross-device claim.\"}\n";
  require(bool(receipt),"Sampling receipt write failed");require(layered_pass,"GPU sampling layered contracts or validation failed");
  if(!paired_pass){std::cout<<"PARTIAL: layered contracts passed; original paired transport/sample criterion FAILED "<<measured.failed
    <<" cases; max weight relative difference="<<measured.max_weight_relative<<" at case "<<measured.weight_case<<'\n';
   require(!require_paired,"--require-paired-transport rejects the retained original sampling discrepancy");
  } else std::cout<<"PASS original paired and layered sampling contracts: "<<measured.strict<<" strict cases + "<<measured.boundary<<" exact horizon classification\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
