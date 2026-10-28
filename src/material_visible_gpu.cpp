// SPDX-License-Identifier: MIT
// Visible-GGX v1 sampled-state conformance. Legacy NDF remains a separate control.
#include "basix/material_vndf.hpp"
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <charconv>
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
struct alignas(16) SamplingResult {float wi_x,wi_y,wi_z,density;float r,g,b;std::uint32_t valid;float hx,hy,hz,projection;};
struct alignas(16) Root {std::uint64_t cases,results;std::uint32_t count,pad0,pad1,pad2;};
static_assert(sizeof(MaterialData)==48&&offsetof(MaterialData,roughness)==12&&offsetof(MaterialData,metallic)==16);
static_assert(offsetof(MaterialData,emission_r)==20&&offsetof(MaterialData,model)==32&&offsetof(MaterialData,pad2)==44);
static_assert(sizeof(SamplingCase)==80&&offsetof(SamplingCase,wo_x)==48&&offsetof(SamplingCase,u_lobe)==60&&offsetof(SamplingCase,u1)==64);
static_assert(sizeof(SamplingResult)==48&&offsetof(SamplingResult,density)==12&&offsetof(SamplingResult,r)==16&&offsetof(SamplingResult,valid)==28);
static_assert(sizeof(Root)==32&&offsetof(Root,results)==8&&offsetof(Root,count)==16);
constexpr std::uint32_t case_count=4096;
std::uint32_t input_seed=0x467a89cf;
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
 Random random;random.state=input_seed;
 while(out.size()<case_count){const auto i=out.size();
  Vec3 base{random.next(),random.next(),random.next()};const double rough=i%11==0?.05:.05+.95*random.next();
  const double metal=i%3==0?0:(i%3==1?1:random.next());const double z=i%17==0?1e-5:.001+.999*random.next();
  out.push_back(make_case(i%5==0?0:1,base,rough,metal,hemisphere(z,2*std::numbers::pi*random.next()),random.next(),random.next(),random.next()));
 }
 return out;
}
static_assert(offsetof(SamplingResult,hx)==32&&offsetof(SamplingResult,projection)==44);
std::vector<std::uint32_t> read_shader(){
 std::ifstream f(shader_directory/"material_visible.spv",std::ios::binary|std::ios::ate);require(bool(f),"Visible shader missing");auto n=f.tellg();require(n>0&&n%4==0,"Invalid visible shader length");std::vector<std::uint32_t> code(size_t(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(code.data()),n);require(bool(f),"Visible shader read failed");return code;
}
basix::render::Material material(const SamplingCase&c){const auto&m=c.material;return {1,{m.base_r,m.base_g,m.base_b},m.roughness,m.metallic,{m.emission_r,m.emission_g,m.emission_b},static_cast<basix::render::MaterialModel>(m.model)};}
struct Measurements{unsigned valid{},null{},failed{},bound_failures{},boundary{};double max_scaled{},max_weight_scaled{},max_direction{},max_bound_error{},max_standalone_pdf_relative{};};
bool close(double a,double b,double absolute=absolute_tolerance){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=absolute+relative_tolerance*std::abs(b);}
Measurements evaluate(Device&device){
 const auto input=cases();size_t offset=sizeof(Root)+input.size()*sizeof(SamplingCase);
 struct Resources{gpu::GpuHeap heap{};gpu::PSO*pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(heap.owner)gpu::destroy_gpu_heap(heap);}}r;
 r.heap=gpu::create_gpu_heap(device.device,offset+input.size()*sizeof(SamplingResult));require(r.heap.owner&&r.heap.range.cpu&&r.heap.range.gpu,"Visible heap unavailable");auto address=reinterpret_cast<uint64_t>(r.heap.range.gpu);auto*root=reinterpret_cast<Root*>(r.heap.range.cpu);*root={address+sizeof(Root),address+offset,case_count,0,0,0};std::memcpy(r.heap.range.cpu+sizeof(Root),input.data(),input.size()*sizeof(SamplingCase));auto*output=reinterpret_cast<SamplingResult*>(r.heap.range.cpu+offset);std::memset(output,0xff,input.size()*sizeof(SamplingResult));
 auto code=read_shader();r.pso=gpu::create_compute_pso(device.device,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={64,1,1}});require(r.pso,"Visible pipeline creation failed");auto*c=gpu::begin_commands(device.pool);gpu::bind_pso(c,r.pso);gpu::dispatch(c,r.heap.range.gpu,{case_count/64,1,1});VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};memory.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;memory.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;memory.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;memory.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&memory;vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(gpu::basix_native_commands(c)),&dependency);gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=device.timeline,.value=1};gpu::submit(device.device,{.commands={c},.completion=done});gpu::wait_timeline(done);
 Measurements result;using namespace basix::render;
 for(size_t i=0;i<input.size();++i){const auto&v=input[i];const auto m=material(v);Vec3 wo{v.wo_x,v.wo_y,v.wo_z};auto ref=sample_visible(m,wo,v.u_lobe,v.u1,v.u2);const auto&a=output[i];bool pass=true;
  if(i==exact_horizon_case){++result.boundary;pass=a.valid==0?(a.wi_x==0&&a.wi_y==0&&a.wi_z==0&&a.density==0&&a.r==0&&a.g==0&&a.b==0&&a.hx==0&&a.hy==0&&a.hz==0&&a.projection==0):(a.valid==1&&std::isfinite(a.wi_z)&&a.wi_z>0&&a.wi_z<=boundary_z_tolerance&&std::abs(a.wi_x-1)<=direction_tolerance&&std::abs(a.wi_y)<=direction_tolerance&&std::isfinite(a.density)&&a.density>0&&std::isfinite(a.r)&&std::isfinite(a.g)&&std::isfinite(a.b)&&a.r>=0&&a.g>=0&&a.b>=0);}
  else if(!ref.valid){++result.null;pass=a.valid==0&&a.wi_x==0&&a.wi_y==0&&a.wi_z==0&&a.density==0&&a.r==0&&a.g==0&&a.b==0&&a.hx==0&&a.hy==0&&a.hz==0&&a.projection==0;}
  else{++result.valid;pass=a.valid==1;double actual[]={a.wi_x,a.wi_y,a.wi_z,a.r,a.g,a.b,a.density,a.hx,a.hy,a.hz,a.projection};double expected[]={ref.wi.x,ref.wi.y,ref.wi.z,ref.f.x,ref.f.y,ref.f.z,ref.pdf,ref.half.x,ref.half.y,ref.half.z,ref.projection};
   for(unsigned k=0;k<11;++k){double error=std::abs(actual[k]-expected[k]);if(k<3||(k>=7&&k<=9)){if(std::isfinite(error))result.max_direction=std::max(result.max_direction,error);pass=pass&&std::isfinite(actual[k])&&error<=direction_tolerance;}else{if(std::isfinite(error))result.max_scaled=std::max(result.max_scaled,error/(absolute_tolerance+relative_tolerance*std::abs(expected[k])));pass=pass&&actual[k]>=0&&close(actual[k],expected[k]);}}
   const double actual_f[]={a.r,a.g,a.b},expected_f[]={ref.f.x,ref.f.y,ref.f.z};
   for(unsigned k=0;k<3;++k){double aw=actual_f[k]*a.wi_z/a.density,ew=expected_f[k]*ref.wi.z/ref.pdf;double error=std::abs(aw-ew);double tolerance=3e-5+relative_tolerance*std::abs(ew);if(std::isfinite(error))result.max_weight_scaled=std::max(result.max_weight_scaled,error/tolerance);pass=pass&&std::isfinite(aw)&&aw>=0&&error<=tolerance;
    if(m.model==MaterialModel::metallic_roughness&&m.metallic==1){auto n=normalize(wo);double cosine=std::clamp(n.x*a.hx+n.y*a.hy+n.z*a.hz,0.,1.);double tint=k==0?m.base_color.x:(k==1?m.base_color.y:m.base_color.z);double fresnel=tint+(1-tint)*std::pow(1-cosine,5);double alpha=m.roughness*m.roughness;double expected_bound=fresnel*2*a.wi_z/(a.wi_z+std::sqrt(alpha*alpha+(1-alpha*alpha)*double(a.wi_z)*a.wi_z));double error_bound=std::abs(aw-expected_bound);result.max_bound_error=std::max(result.max_bound_error,error_bound);if(!(aw<=1.00001&&error_bound<=3e-5+relative_tolerance*std::abs(expected_bound)))++result.bound_failures;}
   }
   const double reconstructed=pdf_visible(m,wo,{a.wi_x,a.wi_y,a.wi_z});if(std::isfinite(reconstructed)&&reconstructed>0)result.max_standalone_pdf_relative=std::max(result.max_standalone_pdf_relative,std::abs(a.density-reconstructed)/reconstructed);
  }
  if(!pass){if(result.failed<12)std::cerr<<"Visible case "<<i<<" valid="<<a.valid<<'/'<<ref.valid<<" wi.z="<<a.wi_z<<'/'<<ref.wi.z<<" pdf="<<a.density<<'/'<<ref.pdf<<'\n';++result.failed;}
 }
 return result;
}
}
int main(int argc,char**argv){try{bool validate=false;std::filesystem::path out="visible-output";for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--validate")validate=true;else if(a=="--output"&&i+1<argc)out=argv[++i];else if(a=="--seed"&&i+1<argc){const std::string value=argv[++i];auto parsed=std::from_chars(value.data(),value.data()+value.size(),input_seed);require(parsed.ec==std::errc()&&parsed.ptr==value.data()+value.size(),"Invalid seed");}else if(a=="--shader-dir"&&i+1<argc)shader_directory=argv[++i];else throw std::runtime_error("Invalid visible argument");}
 std::filesystem::create_directories(out);Measurements measured;bool validation=false;{Device device;device.initialize(validate);validation=device.validation;measured=evaluate(device);}bool pass=measured.failed==0&&measured.bound_failures==0&&errors==0&&warnings==0;
 std::ofstream f(out/"result.json");f<<std::setprecision(12)<<"{\"status\":\""<<(pass?"passed":"failed")<<"\",\"method\":\"visible_ggx_v1_sampled_state\",\"input_seed\":"<<input_seed<<",\"cases\":4096,\"strict_cases\":4095,\"exact_horizon_classifications\":"<<measured.boundary<<",\"valid\":"<<measured.valid<<",\"null\":"<<measured.null<<",\"failed_cases\":"<<measured.failed<<",\"bound_failures\":"<<measured.bound_failures<<",\"max_f_pdf_error_over_tolerance\":"<<measured.max_scaled<<",\"max_weight_error_over_tolerance\":"<<measured.max_weight_scaled<<",\"max_direction_or_half_error\":"<<measured.max_direction<<",\"max_bound_formula_error\":"<<measured.max_bound_error<<",\"standalone_reconstruction_max_pdf_relative_difference\":"<<measured.max_standalone_pdf_relative<<",\"absolute_tolerance\":0.0003,\"relative_tolerance\":0.001,\"direction_tolerance\":0.00002,\"weight_absolute_tolerance\":0.00003,\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<errors<<",\"validation_warnings\":"<<warnings<<",\"scope\":\"Distinct VNDF sampled-normal state contract; legacy NDF oracle unchanged. Rounded-direction standalone reconstruction is a separate conditioning diagnostic, not a replacement pass for legacy NDF.\"}\n";f.close();require(bool(f),"Visible receipt write failed");require(pass,"Visible sampled-state conformance failed");std::cout<<"PASS visible sampled-state "<<case_count<<" cases; maxweight fraction="<<measured.max_weight_scaled<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
