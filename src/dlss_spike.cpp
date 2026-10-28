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
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void check(sl::Result r,const char* name){if(r!=sl::Result::eOk)throw std::runtime_error(std::string(name)+" result="+std::to_string(static_cast<unsigned>(r)));}
void vkcheck(VkResult r,const char* name){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+" VkResult="+std::to_string(r));}
std::atomic<unsigned> errors{0},warnings{0},teardown_errors{0};
VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;else if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)++warnings;
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)std::cerr<<"Basix validation: "<<data->pMessage<<'\n';return VK_FALSE;
}
struct FeaturePlan {
 sl::FeatureRequirements required{};
 std::vector<const char*> instance_extensions,device_extensions;
 std::vector<std::string> normalized;
 VkPhysicalDeviceVulkan12Features want12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
 VkPhysicalDeviceVulkan13Features want13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
 explicit FeaturePlan(basix::StreamlineRuntime& sdk){
  check(sdk.requirements(required),"slGetFeatureRequirements");
  require(required.vkNumInstanceExtensions<=48&&required.vkNumDeviceExtensions<=48,"SDK extension list too large");
  require(required.vkNumComputeQueuesRequired==0&&required.vkNumGraphicsQueuesRequired==0&&required.vkNumOpticalFlowQueuesRequired==0,"Additional SDK-owned queues not implemented in this bounded SR probe");
  for(uint32_t i=0;i<required.vkNumInstanceExtensions;++i){auto n=required.vkInstanceExtensions[i];std::cout<<"Required instance extension "<<n<<'\n';instance_extensions.push_back(n);}
  for(uint32_t i=0;i<required.vkNumDeviceExtensions;++i){auto n=required.vkDeviceExtensions[i];std::cout<<"Required device extension "<<n<<'\n';if(std::strcmp(n,"VK_EXT_buffer_device_address")==0){normalized.emplace_back(n);continue;}device_extensions.push_back(n);}
  want12=sl::getVkPhysicalDeviceVulkan12Features(required.vkNumFeatures12,required.vkFeatures12);
  want13=sl::getVkPhysicalDeviceVulkan13Features(required.vkNumFeatures13,required.vkFeatures13);
  for(uint32_t i=0;i<required.vkNumFeatures12;++i){auto one=sl::getVkPhysicalDeviceVulkan12Features(1,&required.vkFeatures12[i]);require(feature_count(one,offsetof(VkPhysicalDeviceVulkan12Features,samplerMirrorClampToEdge),offsetof(VkPhysicalDeviceVulkan12Features,subgroupBroadcastDynamicId)+4)==1,"Unrecognized SDK Vulkan1.2 feature");}
  for(uint32_t i=0;i<required.vkNumFeatures13;++i){auto one=sl::getVkPhysicalDeviceVulkan13Features(1,&required.vkFeatures13[i]);require(feature_count(one,offsetof(VkPhysicalDeviceVulkan13Features,robustImageAccess),offsetof(VkPhysicalDeviceVulkan13Features,maintenance4)+4)==1,"Unrecognized SDK Vulkan1.3 feature");}
  // SL common creates a private data slot even though SR's returned list omits
  // this core1.3 requirement. The first validation run exposed the missing bit.
  want13.privateData=VK_TRUE;
  for(uint32_t i=0;i<required.vkNumFeatures12;++i)std::cout<<"Required Vulkan1.2 feature "<<required.vkFeatures12[i]<<'\n';
  for(uint32_t i=0;i<required.vkNumFeatures13;++i)std::cout<<"Required Vulkan1.3 feature "<<required.vkFeatures13[i]<<'\n';
 }
 template<class T> static unsigned feature_count(const T& features,size_t offset,size_t end){unsigned count=0;for(size_t n=offset;n<end;n+=4){VkBool32 value{};std::memcpy(&value,reinterpret_cast<const char*>(&features)+n,4);count+=value!=0;}return count;}
 template<class T> static bool merge(T& enabled,const T& wanted,const T& supported,size_t offset,size_t end)noexcept{
  for(size_t n=offset;n<end;n+=sizeof(VkBool32)){
   VkBool32 e{},w{},s{};std::memcpy(&e,reinterpret_cast<const char*>(&enabled)+n,4);std::memcpy(&w,reinterpret_cast<const char*>(&wanted)+n,4);std::memcpy(&s,reinterpret_cast<const char*>(&supported)+n,4);
   if(w&&!s)return false;e=e||w;std::memcpy(reinterpret_cast<char*>(&enabled)+n,&e,4);
  }return true;
 }
 static bool configure(void* physical,void* v12,void* v13,void* user)noexcept{
  auto& p=*static_cast<FeaturePlan*>(user);VkPhysicalDeviceVulkan13Features s13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceVulkan12Features s12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};s12.pNext=&s13;VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&s12;vkGetPhysicalDeviceFeatures2(static_cast<VkPhysicalDevice>(physical),&f);
  return merge(*static_cast<VkPhysicalDeviceVulkan12Features*>(v12),p.want12,s12,offsetof(VkPhysicalDeviceVulkan12Features,samplerMirrorClampToEdge),offsetof(VkPhysicalDeviceVulkan12Features,subgroupBroadcastDynamicId)+sizeof(VkBool32))&&merge(*static_cast<VkPhysicalDeviceVulkan13Features*>(v13),p.want13,s13,offsetof(VkPhysicalDeviceVulkan13Features,robustImageAccess),offsetof(VkPhysicalDeviceVulkan13Features,maintenance4)+sizeof(VkBool32));
 }
};
struct Device {
 basix::StreamlineRuntime& sdk;gpu::Device* gpu=nullptr;gpu::CommandPool* pool=nullptr;gpu::TimelineSemaphore* timeline=nullptr;uint64_t serial=0;
 gpu::NativeVulkanHandles native{};VkDevice device=nullptr;VkDebugUtilsMessengerEXT messenger=nullptr;PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger=nullptr;
 explicit Device(basix::StreamlineRuntime& s):sdk(s){}
 ~Device(){if(gpu){gpu::wait_idle(gpu);if(sdk.shutdown()!=sl::Result::eOk){++teardown_errors;std::cerr<<"SDK teardown failed; retaining Vulkan objects until process exit\n";return;}if(pool)gpu::destroy_command_pool(pool);if(timeline)gpu::destroy_timeline_semaphore(timeline);if(messenger)destroy_messenger(static_cast<VkInstance>(native.instance),messenger,nullptr);gpu::destroy_device(gpu);}}
 void initialize(FeaturePlan& p,bool validation){
  gpu::set_error_callback([](const char* m,bool)noexcept{std::cerr<<"NoGraphicsAPI: "<<m<<'\n';});
  gpu::DeviceDesc d{};d.basix_instance_extensions=p.instance_extensions.data();d.basix_instance_extension_count=static_cast<gpu::uint32>(p.instance_extensions.size());d.basix_device_extensions=p.device_extensions.data();d.basix_device_extension_count=static_cast<gpu::uint32>(p.device_extensions.size());d.basix_configure_features=FeaturePlan::configure;d.basix_feature_user=&p;
  const auto r=gpu::create_device(d);gpu=r.device;require(gpu,"NoGraphicsAPI SDK-capable device creation failed");native=gpu::basix_native_vulkan(gpu);device=static_cast<VkDevice>(native.device);require(!validation||native.validation_enabled,"Required validation layer not enabled");
  if(native.validation_enabled){auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(static_cast<VkInstance>(native.instance),"vkCreateDebugUtilsMessengerEXT"));destroy_messenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(static_cast<VkInstance>(native.instance),"vkDestroyDebugUtilsMessengerEXT"));require(create&&destroy_messenger,"Debug utils unavailable");VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;info.pfnUserCallback=diagnostic;vkcheck(create(static_cast<VkInstance>(native.instance),&info,nullptr,&messenger),"debug messenger");}
  sl::VulkanInfo info{};info.device=device;info.instance=static_cast<VkInstance>(native.instance);info.physicalDevice=static_cast<VkPhysicalDevice>(native.physical_device);info.graphicsQueueFamily=info.computeQueueFamily=gpu::basix_graphics_queue_family(gpu);info.graphicsQueueIndex=info.computeQueueIndex=1;
  check(sdk.bind_vulkan(info),"slSetVulkanInfo");check(sdk.supports(info.physicalDevice),"slIsFeatureSupported");
  pool=gpu::create_command_pool(gpu);timeline=gpu::create_timeline_semaphore(gpu);require(pool&&timeline,"GPU command resources failed");
 }
 gpu::CommandBuffer* begin(){gpu::reset_command_pool(pool);return gpu::begin_commands(pool);}
 void submit(gpu::CommandBuffer* c){gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=timeline,.value=++serial};gpu::submit(gpu,{.commands={c},.completion=done});gpu::wait_timeline(done);}
};
struct Image {
 Device& owner;gpu::TextureHeap heap{};gpu::Texture* texture=nullptr;VkImage image=nullptr;VkImageView view=nullptr;uint32_t width,height;VkFormat format;VkImageUsageFlags usage;
 Image(Device& d,uint32_t w,uint32_t h,gpu::Format gf,VkFormat vf):owner(d),width(w),height(h),format(vf),usage(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT){
  try{
   VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(static_cast<VkPhysicalDevice>(d.native.physical_device),vf,&properties);require((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT))==(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT),"Image format lacks required capabilities");
   const gpu::TextureDesc desc{.extent={w,h,1},.format=gf,.usage=gpu::TextureUsage::sampled|gpu::TextureUsage::storage|gpu::TextureUsage::transfer_destination|gpu::TextureUsage::transfer_source};const auto size=gpu::get_texture_size_align(d.gpu,desc);heap=gpu::create_texture_heap(d.gpu,size.size);require(heap.owner,"Image heap allocation failed");texture=gpu::create_texture(d.gpu,desc,heap,0);require(texture,"Image creation failed");image=reinterpret_cast<VkImage>(gpu::basix_native_texture(texture).image);
   VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=vf;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkcheck(vkCreateImageView(d.device,&vi,nullptr,&view),"image view");
  }catch(...){release();throw;}
 }
 void release(){if(view)vkDestroyImageView(owner.device,view,nullptr);if(texture)gpu::destroy_texture(texture);if(heap.owner)gpu::destroy_texture_heap(heap);}
 ~Image(){release();}Image(const Image&)=delete;Image& operator=(const Image&)=delete;
 sl::Resource resource()const{sl::Resource r{sl::ResourceType::eTex2d,reinterpret_cast<void*>(image),nullptr,reinterpret_cast<void*>(view),VK_IMAGE_LAYOUT_GENERAL};r.width=width;r.height=height;r.nativeFormat=format;r.mipLevels=1;r.arrayLayers=1;r.flags=0;r.usage=usage;return r;}
};
struct NativeCopy {
 Device& owner;VkBuffer buffer=nullptr;VkDeviceMemory memory=nullptr;void* mapped=nullptr;
 NativeCopy(Device& d,uint64_t size):owner(d){try{VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=size;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;vkcheck(vkCreateBuffer(d.device,&b,nullptr,&buffer),"native copy buffer");VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d.device,buffer,&req);VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(static_cast<VkPhysicalDevice>(d.native.physical_device),&props);uint32_t type=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}require(type!=UINT32_MAX,"Native copy host memory unavailable");VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;vkcheck(vkAllocateMemory(d.device,&a,nullptr,&memory),"native copy memory");vkcheck(vkBindBufferMemory(d.device,buffer,memory,0),"native copy bind");vkcheck(vkMapMemory(d.device,memory,0,size,0,&mapped),"native copy map");}catch(...){release();throw;}}
 void release(){if(mapped)vkUnmapMemory(owner.device,memory);if(buffer)vkDestroyBuffer(owner.device,buffer,nullptr);if(memory)vkFreeMemory(owner.device,memory,nullptr);}
 ~NativeCopy(){release();}NativeCopy(const NativeCopy&)=delete;NativeCopy& operator=(const NativeCopy&)=delete;
 void copy(VkCommandBuffer c,const Image& image){VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};b.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;b.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.imageMemoryBarrierCount=1;dep.pImageMemoryBarriers=&b;vkCmdPipelineBarrier2(c,&dep);VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={image.width,image.height,1};vkCmdCopyImageToBuffer(c,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);b.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;b.srcAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;b.dstAccessMask=VK_ACCESS_2_SHADER_STORAGE_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;vkCmdPipelineBarrier2(c,&dep);}
};
float from_half(uint16_t value){const int exponent=(value>>10)&31;const float sign=(value&0x8000)?-1.f:1.f;const unsigned fraction=value&1023;if(exponent==31)return fraction?std::numeric_limits<float>::quiet_NaN():sign*std::numeric_limits<float>::infinity();return sign*std::ldexp(exponent?1.f+float(fraction)/1024:float(fraction)/1024,exponent?exponent-15:-14);}
struct LegacyHandoff {
 Device& owner;VkDescriptorSetLayout set_layout=nullptr;VkPipelineLayout layout=nullptr;VkDescriptorPool pool=nullptr;VkDescriptorSet set=nullptr;
 explicit LegacyHandoff(Device& d):owner(d){try{VkDescriptorSetLayoutCreateInfo s{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};vkcheck(vkCreateDescriptorSetLayout(d.device,&s,nullptr,&set_layout),"legacy handoff set layout");VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&set_layout;vkcheck(vkCreatePipelineLayout(d.device,&l,nullptr,&layout),"legacy handoff pipeline layout");VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};p.maxSets=1;vkcheck(vkCreateDescriptorPool(d.device,&p,nullptr,&pool),"legacy handoff pool");VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=pool;a.descriptorSetCount=1;a.pSetLayouts=&set_layout;vkcheck(vkAllocateDescriptorSets(d.device,&a,&set),"legacy handoff allocation");}catch(...){release();throw;}}
 void release(){if(pool)vkDestroyDescriptorPool(owner.device,pool,nullptr);if(layout)vkDestroyPipelineLayout(owner.device,layout,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(owner.device,set_layout,nullptr);}
 ~LegacyHandoff(){release();}LegacyHandoff(const LegacyHandoff&)=delete;LegacyHandoff& operator=(const LegacyHandoff&)=delete;
 void bind(VkCommandBuffer c){vkCmdBindDescriptorSets(c,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);}
};
void barrier(VkCommandBuffer c){VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};b.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dep.memoryBarrierCount=1;dep.pMemoryBarriers=&b;vkCmdPipelineBarrier2(c,&dep);}
sl::Constants constants(uint32_t w,uint32_t h,bool reset){
 sl::Constants c{};auto identity=[](){sl::float4x4 m;for(unsigned row=0;row<4;++row)m[row]={0,0,0,0};m[0].x=m[1].y=m[2].z=m[3].w=1;return m;};
 const float near=.1f,far=100,yscale=1/std::tan(.5f),xscale=yscale/(float(w)/h),a=far/(far-near),b=-far*near/(far-near);
 c.cameraViewToClip=identity();c.cameraViewToClip[0].x=xscale;c.cameraViewToClip[1].y=yscale;c.cameraViewToClip[2]={0,0,a,1};c.cameraViewToClip[3]={0,0,b,0};
 c.clipToCameraView=identity();c.clipToCameraView[0].x=1/xscale;c.clipToCameraView[1].y=1/yscale;c.clipToCameraView[2]={0,0,0,1/b};c.clipToCameraView[3]={0,0,1,-a/b};
 c.clipToLensClip=c.clipToPrevClip=c.prevClipToClip=identity();c.jitterOffset={0,0};c.mvecScale={1,1};c.cameraPinholeOffset={0,0};c.cameraPos={0,0,0};c.cameraUp={0,1,0};c.cameraRight={1,0,0};c.cameraFwd={0,0,1};c.cameraNear=near;c.cameraFar=far;c.cameraFOV=1;c.cameraAspectRatio=float(w)/h;c.motionVectorsInvalidValue=0;c.depthInverted=sl::Boolean::eFalse;c.cameraMotionIncluded=sl::Boolean::eTrue;c.motionVectors3D=sl::Boolean::eFalse;c.reset=reset?sl::Boolean::eTrue:sl::Boolean::eFalse;c.orthographicProjection=sl::Boolean::eFalse;c.motionVectorsDilated=sl::Boolean::eFalse;c.motionVectorsJittered=sl::Boolean::eFalse;return c;
}
struct EvaluationResult {uint32_t input_width,input_height,output_width,output_height,frames;uint64_t pixel_checks;double rgb_rmse,max_error;};
EvaluationResult evaluate(Device& d,const std::filesystem::path& out,bool skip_handoff,bool omit_evaluation,bool fail_after_evaluation){
 auto& sdk=d.sdk;const sl::ViewportHandle viewport{0};
 auto get_settings=sdk.feature_function<PFun_slDLSSGetOptimalSettings>("slDLSSGetOptimalSettings");auto set_options=sdk.feature_function<PFun_slDLSSSetOptions>("slDLSSSetOptions");sl::DLSSOptions options{};options.mode=sl::DLSSMode::eMaxQuality;options.outputWidth=640;options.outputHeight=360;options.useAutoExposure=sl::Boolean::eFalse;sl::DLSSOptimalSettings optimal{};check(get_settings(options,optimal),"slDLSSGetOptimalSettings");const auto w=optimal.optimalRenderWidth,h=optimal.optimalRenderHeight,ow=options.outputWidth,oh=options.outputHeight;require(w&&h&&w<=ow&&h<=oh,"Invalid optimal input dimensions");check(set_options(viewport,options),"slDLSSSetOptions");
 Image color(d,w,h,gpu::Format::rgba16_float,VK_FORMAT_R16G16B16A16_SFLOAT),output(d,ow,oh,gpu::Format::rgba16_float,VK_FORMAT_R16G16B16A16_SFLOAT),depth(d,w,h,gpu::Format::r32_float,VK_FORMAT_R32_SFLOAT),motion(d,w,h,gpu::Format::rg32_float,VK_FORMAT_R32G32_SFLOAT),exposure(d,1,1,gpu::Format::r32_float,VK_FORMAT_R32_SFLOAT);
 struct Resources {gpu::TextureDescriptorHeap* textures=nullptr;gpu::SamplerDescriptorHeap* samplers=nullptr;gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(textures)gpu::destroy_texture_descriptor_heap(textures);if(samplers)gpu::destroy_sampler_descriptor_heap(samplers);if(heap.owner)gpu::destroy_gpu_heap(heap);}} r;
 r.textures=gpu::create_texture_descriptor_heap(d.gpu,2);r.samplers=gpu::create_sampler_descriptor_heap(d.gpu,1);require(r.textures&&r.samplers,"Descriptor heaps failed");gpu::write_texture_descriptor(r.textures,0,color.texture,gpu::TextureDescriptorType::storage);gpu::write_texture_descriptor(r.textures,1,output.texture,gpu::TextureDescriptorType::storage);gpu::write_sampler_descriptor(r.samplers,0);
 r.heap=gpu::create_gpu_heap(d.gpu,128+uint64_t(ow)*oh*16);require(r.heap.owner&&r.heap.range.cpu,"Readback heap failed");struct alignas(16) Root{uint64_t output;uint32_t index,width,height,mode,pad0,pad1;};static_assert(sizeof(Root)==32);auto* roots=reinterpret_cast<Root*>(r.heap.range.cpu);roots[0]={reinterpret_cast<uint64_t>(r.heap.range.gpu)+128,0,w,h,0,0,0};roots[1]={roots[0].output,1,ow,oh,1,0,0};roots[2]={roots[0].output,0,w,h,1,0,0};
 std::ifstream file(std::filesystem::path(BASIX_SHADER_DIR)/"dlss_readback.spv",std::ios::binary|std::ios::ate);require(bool(file),"Readback shader missing");auto bytes=file.tellg();require(bytes>0&&bytes%4==0,"Invalid shader bytes");std::vector<uint32_t> code(static_cast<size_t>(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);require(bool(file),"Readback shader read failed");r.pso=gpu::create_compute_pso(d.gpu,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={8,8,1}});require(r.pso,"Readback pipeline failed");
 auto* init=d.begin();for(auto* image:std::array<Image*,5>{&color,&output,&depth,&motion,&exposure})gpu::initialize_texture(init,image->texture);auto native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(init));VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};const VkClearColorValue exposure_value{{1,0,0,0}};vkCmdClearColorImage(native,exposure.image,VK_IMAGE_LAYOUT_GENERAL,&exposure_value,1,&range);const VkClearColorValue depth_value{{.5f,0,0,0}},zero{{0,0,0,0}},sentinel{{-1000,-1000,-1000,-1000}};vkCmdClearColorImage(native,depth.image,VK_IMAGE_LAYOUT_GENERAL,&depth_value,1,&range);vkCmdClearColorImage(native,motion.image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);vkCmdClearColorImage(native,output.image,VK_IMAGE_LAYOUT_GENERAL,&sentinel,1,&range);barrier(native);d.submit(init);
 NativeCopy native_copy(d,uint64_t(ow)*oh*8);LegacyHandoff legacy(d);
 struct FeatureRelease {Device& d;sl::ViewportHandle viewport{0};bool active=true;void close(){gpu::wait_idle(d.gpu);check(d.sdk.api().slFreeResources(sl::kFeatureDLSS,viewport),"slFreeResources");active=false;}~FeatureRelease(){if(active){gpu::wait_idle(d.gpu);if(d.sdk.api().slFreeResources(sl::kFeatureDLSS,viewport)!=sl::Result::eOk){++teardown_errors;std::cerr<<"slFreeResources fallback failed\n";}}}} feature_release{d};
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
