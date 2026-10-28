// SPDX-License-Identifier: MIT
// Independent analytic / half-slope quadrature targets; never calls CPU BSDF code.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef BASIX_ORACLE_CPU_ONLY
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <vulkan/vulkan.h>
#endif
namespace {
#ifndef BASIX_ORACLE_CPU_ONLY
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
#else
std::atomic<unsigned> errors{0},warnings{0};
std::filesystem::path shader_directory=".";
void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
#endif
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
constexpr double pi=std::numbers::pi;
constexpr unsigned bins=16,bin_count=65;
// These contracts precede all GPU observations. Refinement envelopes are
// numerical diagnostics, not rigorous interval-arithmetic proofs.
constexpr double family_alpha=1e-6,float_hist_floor=2e-5,float_energy_floor=2e-4;
constexpr double quadrature_limit=2e-5,weight_bound=1.0003;
struct Config {const char* name;bool lambert;float rough,z;bool mixed=false;};
constexpr std::array<Config,9> configs{{
 {"lambert_normal",true,.5f,1},{"lambert_grazing",true,.5f,1e-5f},
 {"white_ggx_rough1_normal",false,1,1},{"white_ggx_rough1_grazing",false,1,1e-5f},
 {"white_ggx_rough005_normal",false,.05f,1},{"white_ggx_rough005_grazing",false,.05f,1e-5f},
 {"white_ggx_rough03_oblique",false,.3f,.1f},{"white_ggx_rough005_oblique",false,.05f,.1f},
 {"white_dielectric_mixture",false,.6f,.35f,true}}};
struct Physical {double a,z,s,den;};
Physical physical(Config c){
 // Exact uploaded float wo is normalized by the candidate before sampling.
 const float sx=float(std::sqrt(1-double(c.z)*c.z));
 const double norm=std::hypot(double(sx),double(c.z)),z=c.z/norm,s=sx/norm,a=double(c.rough)*c.rough;
 return {a,z,s,z+std::sqrt(a*a+(1-a*a)*z*z)};
}
using Hist=std::array<double,bin_count>;
struct Targets {Hist hist{};double energy{},hist_envelope{},energy_envelope{};unsigned hist_order{},energy_order{};};
using Nodes=std::vector<std::pair<double,double>>;
const Nodes& nodes(unsigned n){
 static std::map<unsigned,Nodes> cache;auto found=cache.find(n);if(found!=cache.end())return found->second;
 Nodes out;
 for(unsigned i=1;i<=n;++i){double x=std::cos(pi*(i-.25)/(n+.5)),derivative=0;
  for(unsigned k=0;k<40;++k){double p0=1,p1=x;for(unsigned j=2;j<=n;++j){double p=((2*j-1)*x*p1-(j-1)*p0)/j;p0=p1;p1=p;}
   derivative=n*(x*p1-p0)/(x*x-1);double dx=p1/derivative;x-=dx;if(std::abs(dx)<2e-16)break;}
  out.emplace_back((x+1)/2,1/((1-x*x)*derivative*derivative));
 }
 return cache.emplace(n,std::move(out)).first->second;
}
double arc(double lo,double hi,double t,Physical p){
 lo=std::clamp(lo,-1.,1.);hi=std::clamp(hi,-1.,1.);if(hi<=lo)return 0;
 const double j0=(std::acos(lo)-std::acos(hi))/pi;
 const double j1=(std::sqrt(std::max(0.,1-lo*lo))-std::sqrt(std::max(0.,1-hi*hi)))/pi;
 const double value=2*(p.s*t*j1+p.z*j0)/p.den;
 require(value>=-1e-10,"Independent azimuth integral became negative");return std::max(0.,value);
}
Hist radial_hist(double q,Physical p){
 Hist out{};double t=p.a*std::tan(q),tt=t*t,A=2*p.s*t/(1+tt),B=p.z*(1-tt)/(1+tt);
 if(A<=0)return out;
 double discr=std::sqrt(p.z*p.z+2*p.s*p.s*(1+tt));
 double rlo=(-p.z-discr)/(2*p.s*t),rhi=(-p.z+discr)/(2*p.s*t);
 for(unsigned b=0;b<bins;++b){double lo=std::clamp((double(b)/bins-B)/A,-1.,1.),hi=std::clamp((double(b+1)/bins-B)/A,-1.,1.);
  if(hi<=lo)continue;double total=arc(lo,hi,t,p);
  double negative=arc(std::max(lo,rlo),std::min(hi,rhi),t,p),positive=std::max(0.,total-negative);
  out[4*b]=out[4*b+2]=positive*.5;out[4*b+1]=out[4*b+3]=negative*.5;
 }
 return out;
}
Hist histogram_quad(Physical p,unsigned order){
 std::vector<double> cuts{0,pi/2};auto add=[&](double t){if(std::isfinite(t)&&t>0)cuts.push_back(std::atan(t/p.a));};
 const double theta=std::acos(p.z);
 for(unsigned b=0;b<=bins;++b){double z=double(b)/bins,ang=std::acos(z);
  add(std::tan((theta+ang)/2));add(std::tan(std::abs(theta-ang)/2));
  add(std::sqrt(std::max(0.,2-p.z*p.z-z*z))/(p.z+z));
 }
 add(p.z/(1+p.s));add((1+p.s)/p.z);
 std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double x,double y){return std::abs(x-y)<1e-14;}),cuts.end());
 Hist out{};
 for(size_t k=1;k<cuts.size();++k){double lo=cuts[k-1],span=cuts[k]-lo;
  for(auto [x,w]:nodes(order)){double q=lo+span*x;auto h=radial_hist(q,p);double factor=w*span*std::sin(2*q);for(unsigned b=0;b<64;++b)out[b]+=factor*h[b];}
 }
 out[64]=1-std::accumulate(out.begin(),out.begin()+64,0.);return out;
}
double energy_quad(Physical p,unsigned order,bool fresnel=false){
 double qlo=std::atan(p.z/((1+p.s)*p.a)),qhi=std::atan((1+p.s)/(p.z*p.a)),out=0;
 for(auto range:std::array<std::pair<double,double>,2>{{{0,qlo},{qlo,qhi}}}){
  for(auto [u,wu]:nodes(order)){double q=range.first+(range.second-range.first)*u,t=p.a*std::tan(q),hz=1/std::sqrt(1+t*t);
   double k=p.s>0?p.z*(t*t-1)/(2*p.s*t):(t<1?-2:2),angle=std::acos(std::clamp(k,-1.,1.)),subtotal=0;
   for(auto [v,wv]:nodes(std::max(16u,order/2))){double d=(p.s*t*std::cos(angle*v)+p.z)*hz,zi=2*d*hz-p.z;
    if(zi>0){double f=fresnel?.04+.96*std::pow(1-d,5):1;subtotal+=wv*f*4*d*zi/(hz*p.den*(zi+std::sqrt(p.a*p.a+(1-p.a*p.a)*zi*zi)));}}
   out+=wu*(range.second-range.first)*std::sin(2*q)*angle/pi*subtotal;
  }
 }
 return out;
}
Targets target(Config c){Targets out;Physical p=physical(c);
 if(c.lambert){for(unsigned b=0;b<bins;++b)for(unsigned j=0;j<4;++j)out.hist[4*b+j]=(double((b+1)*(b+1))-double(b*b))/(bins*bins*4);out.energy=1;return out;}
 if(p.s==0){
  auto cdf=[&](double z){double t2=(1-z)/(1+z);return t2/(p.a*p.a+t2);};
  for(unsigned b=0;b<bins;++b)for(unsigned j=0;j<4;++j)out.hist[4*b+j]=(cdf(double(b)/bins)-cdf(double(b+1)/bins))/4;
  out.hist[64]=p.a*p.a/(1+p.a*p.a);
 }else if(p.a==1){for(unsigned b=0;b<64;++b)out.hist[b]=1/((1+p.z)*64);out.hist[64]=p.z/(1+p.z);
 }else{
  Hist previous=histogram_quad(p,32);bool converged=false;
  for(unsigned order:{64u,128u,256u,512u}){auto current=histogram_quad(p,order);double delta=0;for(unsigned b=0;b<bin_count;++b)delta=std::max(delta,std::abs(current[b]-previous[b]));
   out.hist=current;out.hist_order=order;out.hist_envelope=8*delta+1e-8;
   if(out.hist_envelope<=quadrature_limit){converged=true;break;}previous=current;}
  require(converged,"Histogram quadrature unresolved at predeclared order cap");
 }
 if(p.a==1)out.energy=2*(1-std::log(2.))/(1+p.z);
 else{double previous=energy_quad(p,128,c.mixed);bool converged=false;
  for(unsigned order:{256u,512u,1024u}){double current=energy_quad(p,order,c.mixed);out.energy=current;out.energy_order=order;out.energy_envelope=8*std::abs(current-previous)+1e-8;
   if(out.energy_envelope<=quadrature_limit){converged=true;break;}previous=current;}
  require(converged,"Energy quadrature unresolved at predeclared order cap");
 }
 if(c.mixed){
  for(unsigned b=0;b<bins;++b)for(unsigned j=0;j<4;++j){double lambert=(double((b+1)*(b+1))-double(b*b))/(bins*bins*4);out.hist[4*b+j]=.5*out.hist[4*b+j]+.5*lambert;}
  out.hist[64]*=.5;out.hist_envelope*=.5;
  const double Fd=.04+.96*std::pow(1-p.z,5);
  out.energy+=(1-Fd)*(.96*20/21);
 }
 for(double x:out.hist)require(std::isfinite(x)&&x>=-1e-8&&x<=1+1e-8,"Independent histogram invalid");
 require(std::abs(std::accumulate(out.hist.begin(),out.hist.end(),0.)-1)<1e-9,"Histogram target normalization failed");
 require(out.energy>0&&out.energy<=1+1e-8,"Independent energy target invalid");return out;
}
// Analytic anchors additionally check the generic numerical integrators.
void oracle_self_checks(){
 Physical p=physical({"anchor",false,1,.1f});auto numerical=histogram_quad(p,128);
 for(unsigned b=0;b<64;++b)require(std::abs(numerical[b]-1/((1+p.z)*64))<2e-6,"Offnormal analytic histogram anchor failed");
 require(std::abs(numerical[64]-p.z/(1+p.z))<2e-6,"Offnormal analytic null anchor failed");
 require(std::abs(energy_quad(p,256)-2*(1-std::log(2.))/(1+p.z))<2e-6,"Offnormal analytic energy anchor failed");
 p=physical({"anchor",false,.3f,1});auto h=target({"anchor",false,.3f,1});
 // Generic offnormal integrator cannot take exactly s=0; analytic normal mass
 // is independently checked via projected-NDF radial CDF normalization.
 require(std::abs(std::accumulate(h.hist.begin(),h.hist.begin()+64,0.)-1/(1+p.a*p.a))<1e-12,"Normal analytic mass anchor failed");
}
std::vector<SamplingCase> make_inputs(Config c,unsigned n,unsigned index){
 std::mt19937_64 rng(0x95d718a43c826e01ULL+index);auto u=[&](){return float(rng()>>40)*0x1p-24f;};
 std::vector<SamplingCase> out(n);for(auto& v:out){v={};v.material={c.lambert?.2f:1.f,c.lambert?.4f:1.f,c.lambert?.8f:1.f,c.rough,(c.lambert||c.mixed)?0.f:1.f,0,0,0,c.lambert?0u:1u,0,0,0};
  v.wo_x=float(std::sqrt(1-double(c.z)*c.z));v.wo_z=c.z;v.u_lobe=u();v.u1=u();v.u2=u();}return out;
}
#ifndef BASIX_ORACLE_CPU_ONLY
std::vector<SamplingResult> dispatch(Device& owner,const std::vector<SamplingCase>& input,std::uint64_t value){
 struct Resources {gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;gpu::Device* device=nullptr;
  ~Resources(){if(device)gpu::wait_idle(device);if(pso)gpu::destroy_pso(pso);if(heap.owner)gpu::destroy_gpu_heap(heap);}} r;r.device=owner.device;
 const size_t input_offset=sizeof(Root),output_offset=input_offset+input.size()*sizeof(SamplingCase);
 r.heap=gpu::create_gpu_heap(owner.device,output_offset+input.size()*sizeof(SamplingResult));require(r.heap.owner&&r.heap.range.cpu&&r.heap.range.gpu,"Mapped distribution heap unavailable");
 auto address=reinterpret_cast<std::uint64_t>(r.heap.range.gpu);*reinterpret_cast<Root*>(r.heap.range.cpu)={address+input_offset,address+output_offset,static_cast<unsigned>(input.size()),0,0,0};
 std::memcpy(r.heap.range.cpu+input_offset,input.data(),input.size()*sizeof(SamplingCase));std::memset(r.heap.range.cpu+output_offset,0xff,input.size()*sizeof(SamplingResult));
 std::ifstream shader(shader_directory/"material_distribution.spv",std::ios::binary|std::ios::ate);require(bool(shader),"Distribution shader missing");auto length=shader.tellg();require(length>0&&length%4==0,"Distribution shader size invalid");
 std::vector<std::uint32_t> code(static_cast<size_t>(length)/4);shader.seekg(0);shader.read(reinterpret_cast<char*>(code.data()),length);require(bool(shader),"Distribution shader read failed");
 r.pso=gpu::create_compute_pso(owner.device,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={64,1,1}});require(r.pso,"Distribution PSO failed");
 auto* command=gpu::begin_commands(owner.pool);gpu::bind_pso(command,r.pso);gpu::dispatch(command,r.heap.range.gpu,{static_cast<unsigned>((input.size()+63)/64),1,1});
 VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};memory.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;memory.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
 memory.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;memory.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&memory;
 vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(gpu::basix_native_commands(command)),&dep);gpu::end_commands(command);
 const gpu::TimelinePoint done{.semaphore=owner.timeline,.value=value};gpu::submit(owner.device,{.commands={command},.completion=done});gpu::wait_timeline(done);
 auto* result=reinterpret_cast<SamplingResult*>(r.heap.range.cpu+output_offset);return {result,result+input.size()};
}
#endif
struct Measurement {std::array<unsigned,bin_count> counts{};std::array<double,3> energy{},energy_tolerance{};unsigned invalid{},hist_failed{},energy_failed{};double max_hist_ratio{},max_energy_ratio{},max_weight{};};
Measurement measure(Config config,const Targets& t,const std::vector<SamplingResult>& samples){
 Measurement m;const double bound=config.mixed?4.:weight_bound;const double n=double(samples.size()),L=std::log(2.*configs.size()*(bin_count+3)/family_alpha);
 for(const auto& s:samples){if(s.valid==0){++m.counts[64];if(s.wi_x||s.wi_y||s.wi_z||s.density||s.r||s.g||s.b)++m.invalid;continue;}
  if(s.valid!=1||!std::isfinite(s.wi_x)||!std::isfinite(s.wi_y)||!std::isfinite(s.wi_z)||s.wi_z<=0||s.wi_z>1.00001||!std::isfinite(s.density)||s.density<=0||std::abs(double(s.wi_x)*s.wi_x+double(s.wi_y)*s.wi_y+double(s.wi_z)*s.wi_z-1)>3e-5){++m.invalid;continue;}
  unsigned b=std::min(15u,static_cast<unsigned>(s.wi_z*16)),q=(s.wi_x>=0?0u:1u)+(s.wi_y>=0?0u:2u);++m.counts[4*b+q];
  const double channels[]={s.r,s.g,s.b},albedo[]={.2f,.4f,.8f};
  for(unsigned j=0;j<3;++j){double weight=channels[j]*s.wi_z/s.density;if(config.lambert)weight/=albedo[j];
   if(!std::isfinite(weight)||weight<0||weight>bound){++m.invalid;continue;}m.max_weight=std::max(m.max_weight,weight);m.energy[j]+=weight/n;}
 }
 for(unsigned b=0;b<bin_count;++b){double p=t.hist[b],tolerance=std::sqrt(2*std::max(0.,p*(1-p))*L/n)+2*L/(3*n)+t.hist_envelope+float_hist_floor;
  double ratio=std::abs(double(m.counts[b])/n-p)/tolerance;m.max_hist_ratio=std::max(m.max_hist_ratio,ratio);if(ratio>1)++m.hist_failed;}
 for(unsigned j=0;j<3;++j){double tolerance=std::sqrt(2*std::max(0.,t.energy*(bound-t.energy))*L/n)+2*bound*L/(3*n)+t.energy_envelope+float_energy_floor;
  m.energy_tolerance[j]=tolerance;double ratio=std::abs(m.energy[j]-t.energy)/tolerance;m.max_energy_ratio=std::max(m.max_energy_ratio,ratio);if(ratio>1)++m.energy_failed;}
 return m;
}
// Adversarial checks of the acceptance machinery: a correct cosine-law
// synthetic population must pass, while a biased PDF or directional population
// must fail. No candidate sample/evaluate implementation supplies these tuples.
void acceptance_self_checks(){
 const Config config={"synthetic_lambert",true,.5f,1};const auto t=target(config);
 std::vector<SamplingResult> population;population.reserve(65536);
 for(unsigned b=0;b<bins;++b)for(unsigned q=0;q<4;++q){
  float z=float((b+.5)/bins),xy=float(std::sqrt((1-double(z)*z)/2));
  SamplingResult v{q%2?-xy:xy,q>=2?-xy:xy,z,float(z/pi),float(.2f/pi),float(.4f/pi),float(.8f/pi),1,0,0,0,0};
  for(unsigned i=0;i<64*(2*b+1);++i)population.push_back(v);
 }
 auto correct=measure(config,t,population);
 require(correct.invalid==0&&correct.hist_failed==0&&correct.energy_failed==0,"Analytic cosine synthetic population rejected");
 for(auto& v:population)v.density*=1.1f;
 require(measure(config,t,population).energy_failed==3,"Biased importance PDF was not detected");
 for(auto& v:population){v.wi_x=0;v.wi_y=0;v.wi_z=1;}
 require(measure(config,t,population).hist_failed>0,"Biased direction distribution was not detected");
}
template<class T,size_t N>void array_json(std::ostream& o,const std::array<T,N>& a){o<<'[';for(size_t i=0;i<N;++i){if(i)o<<',';o<<a[i];}o<<']';}
} // namespace
int main(int argc,char** argv){try{
 std::filesystem::path output="material-distribution-output";bool validate=false,oracle_only=false;unsigned samples=262144;
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--output"&&i+1<argc)output=argv[++i];else if(a=="--shader-dir"&&i+1<argc)shader_directory=argv[++i];else if(a=="--validate")validate=true;else if(a=="--oracle-only")oracle_only=true;
  else if(a=="--samples"&&i+1<argc){std::string value=argv[++i];size_t used=0;auto n=std::stoul(value,&used);require(used==value.size()&&n>=16384&&n<=1048576,"Samples must be integer 16384..1048576");samples=static_cast<unsigned>(n);}else throw std::runtime_error("Usage: --output DIR [--shader-dir DIR] [--validate] [--samples 16384..1048576] [--oracle-only]");}
 require(!oracle_only||!validate,"Oracle-only cannot claim GPU validation");
#ifdef BASIX_ORACLE_CPU_ONLY
 require(oracle_only,"CPU-only build requires --oracle-only");
#endif
 oracle_self_checks();acceptance_self_checks();std::array<Targets,configs.size()> targets;for(size_t i=0;i<configs.size();++i){targets[i]=target(configs[i]);std::cout<<configs[i].name<<" energy="<<std::setprecision(12)<<targets[i].energy<<" histEnvelope="<<targets[i].hist_envelope<<" energyEnvelope="<<targets[i].energy_envelope<<'\n';}
 std::array<Measurement,configs.size()> results{};bool validation=false;
#ifndef BASIX_ORACLE_CPU_ONLY
 if(!oracle_only){Device owner;owner.initialize(validate);validation=owner.validation;for(unsigned i=0;i<configs.size();++i){auto inputs=make_inputs(configs[i],samples,i);auto raw=dispatch(owner,inputs,i+1);results[i]=measure(configs[i],targets[i],raw);}}
#endif
 bool passed=errors==0&&warnings==0;for(const auto& r:results)passed=passed&&r.invalid==0&&r.hist_failed==0&&r.energy_failed==0;
 std::filesystem::create_directories(output);std::ofstream receipt(output/"result.json");receipt<<std::setprecision(15)<<"{\"status\":\""<<(oracle_only?"oracle_only":passed?"passed":"failed")<<"\",\"candidate\":\"visible_normal_v1\",\"gpu_dispatched\":"<<(oracle_only?"false":"true")<<",\"samples_per_config\":"<<samples<<",\"family_alpha\":"<<family_alpha<<",\"weight_bound\":"<<weight_bound<<",\"hist_float_floor\":"<<float_hist_floor<<",\"energy_float_floor\":"<<float_energy_floor<<",\"quadrature_envelope_limit\":"<<quadrature_limit<<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<errors<<",\"validation_warnings\":"<<warnings<<",\"configs\":[";
 for(size_t i=0;i<configs.size();++i){if(i)receipt<<',';const auto& t=targets[i];const auto& r=results[i];receipt<<"{\"name\":\""<<configs[i].name<<"\",\"roughness\":"<<configs[i].rough<<",\"uploaded_wo_z\":"<<configs[i].z<<",\"normalized_weight_bound\":"<<(configs[i].mixed?4.:weight_bound)<<",\"expected_histogram\":";array_json(receipt,t.hist);receipt<<",\"observed_counts\":";array_json(receipt,r.counts);receipt<<",\"normalized_energy_target\":"<<t.energy<<",\"normalized_energy_observed\":";array_json(receipt,r.energy);receipt<<",\"energy_tolerances\":";array_json(receipt,r.energy_tolerance);
  receipt<<",\"hist_order\":"<<t.hist_order<<",\"energy_order\":"<<t.energy_order<<",\"hist_refinement_envelope\":"<<t.hist_envelope<<",\"energy_refinement_envelope\":"<<t.energy_envelope<<",\"invalid_outputs\":"<<r.invalid<<",\"failed_hist_bins\":"<<r.hist_failed<<",\"failed_energy_channels\":"<<r.energy_failed<<",\"max_hist_error_over_tolerance\":"<<r.max_hist_ratio<<",\"max_energy_error_over_tolerance\":"<<r.max_energy_ratio<<",\"max_normalized_weight\":"<<r.max_weight<<'}';}
 receipt<<"],\"method\":\"Independent projected-NDF half-slope radial quadrature with analytic azimuth arcs; Lambert and roughness1 analytic anchors. Energy integrates D-cancelled physical Smith BRDF; white dielectric mixture adds Schlick Fresnel and analytic diffuse integral. Histogram 16 equal-z bins times four sign quadrants plus null. Bonferroni Bernstein confidence assumes iid pseudorandom input variates. Numerical quadrature envelopes are refinement estimates, not rigorous interval bounds.\",\"scope\":\"VNDF candidate only; Lambert, pure white conductor and one white dielectric mixture. Mixture weight bound4, others1.0003. No universal mixed-material energy claim, no legacy NDF discrepancy clearance, no rendered temporal quality claim. Custom validation counters cover post-device-initialization through resource cleanup, not instance creation/destruction; external whole-process log verification remains required.\"}\n";
 require(bool(receipt),"Distribution receipt write failed");require(passed,"Independent GPU distribution or energy or validation gate failed");std::cout<<(oracle_only?"PASS CPU oracle anchors/refinement only":"PASS independent GPU distribution/null/energy gate")<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
