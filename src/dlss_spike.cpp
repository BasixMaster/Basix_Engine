// SPDX-License-Identifier: MIT
// Actual optional SDK correctness probe, not temporal/image-quality acceptance.
#include <basix/streamline_runtime.hpp>
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
#include "dlss_vulkan_support.inl"
struct EvaluationResult {uint32_t input_width,input_height,output_width,output_height,frames;uint64_t pixel_checks;double rgb_rmse,max_error;};
EvaluationResult evaluate(Device& d,const std::filesystem::path& out,bool skip_handoff,bool omit_evaluation,bool fail_after_evaluation){
 auto& sdk=d.sdk();const sl::ViewportHandle viewport{0};
 auto get_settings=sdk.feature_function<PFun_slDLSSGetOptimalSettings>("slDLSSGetOptimalSettings");auto set_options=sdk.feature_function<PFun_slDLSSSetOptions>("slDLSSSetOptions");sl::DLSSOptions options{};options.mode=sl::DLSSMode::eMaxQuality;options.outputWidth=640;options.outputHeight=360;options.useAutoExposure=sl::Boolean::eFalse;sl::DLSSOptimalSettings optimal{};check(get_settings(options,optimal),"slDLSSGetOptimalSettings");const auto w=optimal.optimalRenderWidth,h=optimal.optimalRenderHeight,ow=options.outputWidth,oh=options.outputHeight;require(w&&h&&w<=ow&&h<=oh,"Invalid optimal input dimensions");check(set_options(viewport,options),"slDLSSSetOptions");
 Image color(d,w,h,gpu::Format::rgba16_float,VK_FORMAT_R16G16B16A16_SFLOAT),output(d,ow,oh,gpu::Format::rgba16_float,VK_FORMAT_R16G16B16A16_SFLOAT),depth(d,w,h,gpu::Format::r32_float,VK_FORMAT_R32_SFLOAT),motion(d,w,h,gpu::Format::rg32_float,VK_FORMAT_R32G32_SFLOAT),exposure(d,1,1,gpu::Format::r32_float,VK_FORMAT_R32_SFLOAT);
 struct Resources {gpu::TextureDescriptorHeap* textures=nullptr;gpu::SamplerDescriptorHeap* samplers=nullptr;gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(textures)gpu::destroy_texture_descriptor_heap(textures);if(samplers)gpu::destroy_sampler_descriptor_heap(samplers);if(heap.owner)gpu::destroy_gpu_heap(heap);}} r;
 r.textures=gpu::create_texture_descriptor_heap(d.gpu,2);r.samplers=gpu::create_sampler_descriptor_heap(d.gpu,1);require(r.textures&&r.samplers,"Descriptor heaps failed");gpu::write_texture_descriptor(r.textures,0,color.texture,gpu::TextureDescriptorType::storage);gpu::write_texture_descriptor(r.textures,1,output.texture,gpu::TextureDescriptorType::storage);gpu::write_sampler_descriptor(r.samplers,0);
 r.heap=gpu::create_gpu_heap(d.gpu,128+uint64_t(ow)*oh*16);require(r.heap.owner&&r.heap.range.cpu,"Readback heap failed");struct alignas(16) Root{uint64_t output;uint32_t index,width,height,mode,pad0,pad1;};static_assert(sizeof(Root)==32);auto* roots=reinterpret_cast<Root*>(r.heap.range.cpu);roots[0]={reinterpret_cast<uint64_t>(r.heap.range.gpu)+128,0,w,h,0,0,0};roots[1]={roots[0].output,1,ow,oh,1,0,0};roots[2]={roots[0].output,0,w,h,1,0,0};
 std::ifstream file(std::filesystem::path(BASIX_SHADER_DIR)/"dlss_readback.spv",std::ios::binary|std::ios::ate);require(bool(file),"Readback shader missing");auto bytes=file.tellg();require(bytes>0&&bytes%4==0,"Invalid shader bytes");std::vector<uint32_t> code(static_cast<size_t>(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);require(bool(file),"Readback shader read failed");r.pso=gpu::create_compute_pso(d.gpu,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={8,8,1}});require(r.pso,"Readback pipeline failed");
 auto* init=d.begin();for(auto* image:std::array<Image*,5>{&color,&output,&depth,&motion,&exposure})gpu::initialize_texture(init,image->texture);auto native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(init));VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};const VkClearColorValue exposure_value{{1,0,0,0}};vkCmdClearColorImage(native,exposure.image,VK_IMAGE_LAYOUT_GENERAL,&exposure_value,1,&range);const VkClearColorValue depth_value{{.5f,0,0,0}},zero{{0,0,0,0}},sentinel{{-1000,-1000,-1000,-1000}};vkCmdClearColorImage(native,depth.image,VK_IMAGE_LAYOUT_GENERAL,&depth_value,1,&range);vkCmdClearColorImage(native,motion.image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);vkCmdClearColorImage(native,output.image,VK_IMAGE_LAYOUT_GENERAL,&sentinel,1,&range);barrier(native);d.submit(init);
 NativeCopy native_copy(d,uint64_t(ow)*oh*8);LegacyHandoff legacy(d);
 struct FeatureRelease {Device& d;sl::ViewportHandle viewport{0};bool active=true;void close(){gpu::wait_idle(d.gpu);check(d.sdk().api().slFreeResources(sl::kFeatureDLSS,viewport),"slFreeResources");active=false;}~FeatureRelease(){if(active){gpu::wait_idle(d.gpu);if(d.sdk().api().slFreeResources(sl::kFeatureDLSS,viewport)!=sl::Result::eOk){++teardown_errors;std::cerr<<"slFreeResources fallback failed\n";}}}} feature_release{d};
 if(omit_evaluation)feature_release.active=false;
 EvaluationResult result{w,h,ow,oh,3,0,0,0};double square_sum=0;
 for(uint32_t frame=0;frame<result.frames;++frame){
  sl::FrameToken* token=nullptr;check(sdk.api().slGetNewFrameToken(token,&frame),"slGetNewFrameToken");require(token,"Null SDK frame token");auto values=constants(w,h,frame==0);check(sdk.api().slSetConstants(values,*token,viewport),"slSetConstants");auto input=color.resource(),target=output.resource(),z=depth.resource(),mv=motion.resource(),exp=exposure.resource();const sl::ResourceTag tags[]={{&input,sl::kBufferTypeScalingInputColor,sl::ResourceLifecycle::eValidUntilEvaluate},{&target,sl::kBufferTypeScalingOutputColor,sl::ResourceLifecycle::eValidUntilEvaluate},{&z,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilEvaluate},{&mv,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilEvaluate},{&exp,sl::kBufferTypeExposure,sl::ResourceLifecycle::eValidUntilEvaluate}};
  auto* cmd=d.begin();native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(cmd));gpu::set_texture_descriptor_heap(cmd,r.textures);gpu::set_sampler_descriptor_heap(cmd,r.samplers);gpu::bind_pso(cmd,r.pso);gpu::dispatch(cmd,r.heap.range.gpu,{(w+7)/8,(h+7)/8,1});barrier(native);gpu::dispatch(cmd,r.heap.range.gpu+64,{(w+7)/8,(h+7)/8,1});VkMemoryBarrier2 input_host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};input_host.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;input_host.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;input_host.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;input_host.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;VkDependencyInfo input_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};input_dep.memoryBarrierCount=1;input_dep.pMemoryBarriers=&input_host;vkCmdPipelineBarrier2(native,&input_dep);d.submit(cmd);
  const float* before=reinterpret_cast<const float*>(r.heap.range.cpu+128);for(uint64_t i=0;i<uint64_t(w)*h;++i){require(std::abs(before[i*4]-.25f)<.001f&&std::abs(before[i*4+1]-.5f)<.001f&&std::abs(before[i*4+2]-.75f)<.001f,"Input texture CPU oracle failed before SDK");}
  std::cout<<"Input oracle passed frame "<<frame<<'\n';std::fill_n(reinterpret_cast<float*>(r.heap.range.cpu+128),uint64_t(ow)*oh*4,std::numeric_limits<float>::quiet_NaN());
  std::fill_n(static_cast<uint16_t*>(native_copy.mapped),uint64_t(ow)*oh*4,uint16_t(0x7e00));
  cmd=d.begin();native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(cmd));gpu::set_texture_descriptor_heap(cmd,r.textures);gpu::set_sampler_descriptor_heap(cmd,r.samplers);gpu::bind_pso(cmd,r.pso);
  barrier(native);vkCmdClearColorImage(native,output.image,VK_IMAGE_LAYOUT_GENERAL,&sentinel,1,&range);barrier(native);
  check(sdk.api().slSetTagForFrame(*token,viewport,tags,5,reinterpret_cast<sl::CommandBuffer*>(native)),"slSetTagForFrame");const sl::BaseStructure* inputs[]={&viewport};
  // NVIDIA Streamline issue109: heap-mode state can yield black SR inputs.
  // The validated workaround switches to conventional state before NGX.
  if(!skip_handoff)legacy.bind(native);
  if(!omit_evaluation)check(sdk.api().slEvaluateFeature(sl::kFeatureDLSS,*token,inputs,1,reinterpret_cast<sl::CommandBuffer*>(native)),"slEvaluateFeature");barrier(native);
  native_copy.copy(native,output);
  // SDK conventional descriptor/push state invalidates both native heaps and
  // pointer-root state. All bindings and push data are emitted again here.
  gpu::set_texture_descriptor_heap(cmd,r.textures);gpu::set_sampler_descriptor_heap(cmd,r.samplers);gpu::bind_pso(cmd,r.pso);gpu::dispatch(cmd,r.heap.range.gpu+32,{(ow+7)/8,(oh+7)/8,1});VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};host.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;host.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;host.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;host.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&host;vkCmdPipelineBarrier2(native,&dep);d.submit(cmd);
  if(fail_after_evaluation)throw std::runtime_error("injected SDK evaluation cleanup failure");
  const float* pixels=reinterpret_cast<const float*>(r.heap.range.cpu+128);const float expected[]={.25f,.5f,.75f};const auto* half_pixels=static_cast<const uint16_t*>(native_copy.mapped);std::cout<<"Native output first RGB="<<from_half(half_pixels[0])<<','<<from_half(half_pixels[1])<<','<<from_half(half_pixels[2])<<'\n';
  for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x){const auto index=(uint64_t(y)*ow+x)*4;for(unsigned channel=0;channel<3;++channel){const float value=pixels[index+channel];require(std::isfinite(value)&&value>=-.1f&&value<2,"DLSS output nonfinite/sentinel/range failure");require(value==from_half(half_pixels[index+channel]),"Native copy and restored-engine output disagree");const double error=std::abs(value-expected[channel]);result.max_error=std::max(result.max_error,error);square_sum+=error*error;++result.pixel_checks;}}
  std::ofstream ppm(out/("dlss-frame-"+std::to_string(frame)+".ppm"),std::ios::binary);ppm<<"P6\n"<<ow<<' '<<oh<<"\n255\n";for(uint64_t i=0;i<uint64_t(ow)*oh;++i)for(unsigned channel=0;channel<3;++channel)ppm.put(char(std::lround(std::clamp(pixels[i*4+channel],0.f,1.f)*255)));require(bool(ppm),"DLSS image write failed");
 }
 result.rgb_rmse=std::sqrt(square_sum/result.pixel_checks);std::cout<<"Measured RGB RMSE="<<result.rgb_rmse<<" max_error="<<result.max_error<<'\n';require(result.rgb_rmse<.03&&result.max_error<.15,"DLSS constant-radiance oracle tolerance failed");feature_release.close();return result;
}
}

int main(int argc,char** argv){
 try{
  std::filesystem::path runtime,out="dlss-output";bool validation=false,requirements_only=false,skip_handoff=false,omit_evaluation=false,fail_after_evaluation=false,invalid_extension=false;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--runtime"&&i+1<argc)runtime=argv[++i];else if(arg=="--output"&&i+1<argc)out=argv[++i];else if(arg=="--validate")validation=true;else if(arg=="--requirements-only")requirements_only=true;else if(arg=="--skip-legacy-handoff")skip_handoff=true;else if(arg=="--omit-evaluation")omit_evaluation=true;else if(arg=="--fail-after-evaluation")fail_after_evaluation=true;else if(arg=="--invalid-extension")invalid_extension=true;else throw std::runtime_error("Unknown DLSS probe argument");}
  require(!runtime.empty(),"Explicit SDK runtime directory required");
#if defined(NDEBUG)
  require(!validation,"--validate requires a Debug build");
#endif
  std::filesystem::create_directories(out);basix::StreamlineRuntime sdk(runtime);FeaturePlan plan(sdk);
  if(invalid_extension)plan.device_extensions.push_back("VK_BASIX_intentionally_unavailable");
  if(requirements_only){check(sdk.shutdown(),"slShutdown");std::cout<<"PASS requirements query only; no GPU evaluation\n";return 0;}
  EvaluationResult result{};bool enabled=false;VkPhysicalDeviceProperties properties{};{Device device(sdk);device.initialize(plan,validation);enabled=device.native.validation_enabled;vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(device.native.physical_device),&properties);std::cout<<"SDK and NoGraphicsAPI device registered successfully\n";result=evaluate(device,out,skip_handoff,omit_evaluation,fail_after_evaluation);check(sdk.shutdown(),"slShutdown");}
  require(errors==0&&warnings==0&&teardown_errors==0,"Validation or SDK teardown emitted diagnostics");
  std::ofstream receipt(out/"result.json");
  receipt<<R"({"status":"passed","sdk":"Streamline2.14.16 DLSS SR","device":)"<<std::quoted(properties.deviceName)
   <<R"(,"driver_version_raw":)"<<properties.driverVersion<<R"(,"api_version_raw":)"<<properties.apiVersion
   <<R"(,"frames":)"<<result.frames<<R"(,"input_width":)"<<result.input_width<<R"(,"input_height":)"<<result.input_height
   <<R"(,"output_width":)"<<result.output_width<<R"(,"output_height":)"<<result.output_height
   <<R"(,"rgb_component_checks":)"<<result.pixel_checks<<R"(,"input_component_checks":)"<<uint64_t(result.input_width)*result.input_height*3*result.frames
   <<R"(,"native_engine_component_comparisons":)"<<result.pixel_checks<<R"(,"rgb_rmse":)"<<result.rgb_rmse<<R"(,"max_error":)"<<result.max_error
   <<R"(,"validation_enabled":)"<<(enabled?"true":"false")<<R"(,"validation_errors":)"<<errors<<R"(,"validation_warnings":)"<<warnings
   <<R"(,"feature_free_checked":true,"sdk_shutdown_checked":true,"legacy_handoff":)"<<(skip_handoff?"false":"true")
   <<R"(,"additional_core_features":["Vulkan1.3.privateData","Vulkan1.4.pushDescriptor"],"normalized_extension_requests":[)";
  for(size_t i=0;i<plan.normalized.size();++i){if(i)receipt<<',';receipt<<std::quoted(plan.normalized[i]);}
  receipt<<R"(],"scope":"static constant-radiance SR and same-command NoGraphicsAPI heap/pipeline/root restoration; no temporal quality, RR or FG"})"<<'\n';
  require(bool(receipt),"SDK receipt write failed");
  std::cout<<"PASS actual DLSS SR evaluation and engine readback\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
