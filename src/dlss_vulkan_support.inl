// SPDX-License-Identifier: MIT
// Shared original Vulkan/Streamline support; caller provides headers and namespace.
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
#if defined(NDEBUG)
  // The SDK uses optional debug naming/profiling in Release too. Query and
  // enable the available instance extension without enabling validation.
  uint32_t extension_count=0;vkcheck(vkEnumerateInstanceExtensionProperties(nullptr,&extension_count,nullptr),"enumerate SDK instance extensions");std::vector<VkExtensionProperties> available(extension_count);vkcheck(vkEnumerateInstanceExtensionProperties(nullptr,&extension_count,available.data()),"query SDK instance extensions");
  const bool debug_utils_available=std::any_of(available.begin(),available.end(),[](const auto& p){return std::strcmp(p.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME)==0;});
  const bool debug_utils_requested=std::any_of(instance_extensions.begin(),instance_extensions.end(),[](const char* name){return std::strcmp(name,VK_EXT_DEBUG_UTILS_EXTENSION_NAME)==0;});
  if(debug_utils_available&&!debug_utils_requested){instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);std::cout<<"Optional SDK instance extension VK_EXT_debug_utils enabled\n";}
#endif
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
 basix::StreamlineRuntime* sdk_ptr=nullptr;bool owns=true;gpu::Device* gpu=nullptr;gpu::CommandPool* pool=nullptr;gpu::TimelineSemaphore* timeline=nullptr;uint64_t serial=0;
 gpu::NativeVulkanHandles native{};VkDevice device=nullptr;VkDebugUtilsMessengerEXT messenger=nullptr;PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger=nullptr;
 explicit Device(basix::StreamlineRuntime& s):sdk_ptr(&s){}
 Device()=default;basix::StreamlineRuntime& sdk(){require(sdk_ptr!=nullptr,"SDK is unavailable");return *sdk_ptr;}
 void attach(gpu::Device* external,basix::StreamlineRuntime* runtime=nullptr){owns=false;gpu=external;sdk_ptr=runtime;native=gpu::basix_native_vulkan(gpu);device=static_cast<VkDevice>(native.device);}
 ~Device(){if(gpu&&owns){gpu::wait_idle(gpu);if(sdk().shutdown()!=sl::Result::eOk){++teardown_errors;std::cerr<<"SDK teardown failed; retaining Vulkan objects until process exit\n";return;}if(pool)gpu::destroy_command_pool(pool);if(timeline)gpu::destroy_timeline_semaphore(timeline);if(messenger)destroy_messenger(static_cast<VkInstance>(native.instance),messenger,nullptr);gpu::destroy_device(gpu);}}
 void initialize(FeaturePlan& p,bool validation){
  gpu::set_error_callback([](const char* m,bool)noexcept{std::cerr<<"NoGraphicsAPI: "<<m<<'\n';});
  gpu::DeviceDesc d{};d.basix_instance_extensions=p.instance_extensions.data();d.basix_instance_extension_count=static_cast<gpu::uint32>(p.instance_extensions.size());d.basix_device_extensions=p.device_extensions.data();d.basix_device_extension_count=static_cast<gpu::uint32>(p.device_extensions.size());d.basix_configure_features=FeaturePlan::configure;d.basix_feature_user=&p;
  const auto r=gpu::create_device(d);gpu=r.device;require(gpu,"NoGraphicsAPI SDK-capable device creation failed");native=gpu::basix_native_vulkan(gpu);device=static_cast<VkDevice>(native.device);require(!validation||native.validation_enabled,"Required validation layer not enabled");
  if(native.validation_enabled){auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(static_cast<VkInstance>(native.instance),"vkCreateDebugUtilsMessengerEXT"));destroy_messenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(static_cast<VkInstance>(native.instance),"vkDestroyDebugUtilsMessengerEXT"));require(create&&destroy_messenger,"Debug utils unavailable");VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;info.pfnUserCallback=diagnostic;vkcheck(create(static_cast<VkInstance>(native.instance),&info,nullptr,&messenger),"debug messenger");}
  sl::VulkanInfo info{};info.device=device;info.instance=static_cast<VkInstance>(native.instance);info.physicalDevice=static_cast<VkPhysicalDevice>(native.physical_device);info.graphicsQueueFamily=info.computeQueueFamily=gpu::basix_graphics_queue_family(gpu);info.graphicsQueueIndex=info.computeQueueIndex=1;
  check(sdk().bind_vulkan(info),"slSetVulkanInfo");check(sdk().supports(info.physicalDevice),"slIsFeatureSupported");
  pool=gpu::create_command_pool(gpu);timeline=gpu::create_timeline_semaphore(gpu);require(pool&&timeline,"GPU command resources failed");
 }
 gpu::CommandBuffer* begin(){gpu::reset_command_pool(pool);return gpu::begin_commands(pool);}
 void submit(gpu::CommandBuffer* c){gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=timeline,.value=++serial};gpu::submit(gpu,{.commands={c},.completion=done});gpu::wait_timeline(done);}
};
struct Image {
 Device& owner;gpu::TextureHeap heap{};gpu::Texture* texture=nullptr;VkImage image=nullptr;VkImageView view=nullptr;uint32_t width,height;VkFormat format;VkImageUsageFlags usage;
 Image(Device& d,uint32_t w,uint32_t h,gpu::Format gf,VkFormat vf):owner(d),width(w),height(h),format(vf),usage(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT){
  try{
   VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(static_cast<VkPhysicalDevice>(d.native.physical_device),vf,&properties);require((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))==(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT),"Image format lacks required capabilities");
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
