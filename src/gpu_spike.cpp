// Original MIT experiment: NoGraphicsAPI owns the device, command pools and timelines.
// Raw Vulkan lives only in this explicit interop scope. This is not a complete renderer.
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <algorithm>
#include <charconv>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic<unsigned> validation_errors{0},validation_warnings{0};
std::filesystem::path shader_directory=BASIX_SHADER_DIR;
VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++validation_errors;
 else if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)++validation_warnings;
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)std::cerr<<"Basix validation: "<<(data&&data->pMessage?data->pMessage:"missing diagnostic")<<'\n';
 return VK_FALSE;
}
void vkcheck(VkResult r,const char* what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": "+std::to_string(r));}
void require(bool b,const char* what){if(!b)throw std::runtime_error(what);}
std::vector<uint32_t> shader(const char* name){
 std::ifstream f(shader_directory/name,std::ios::binary|std::ios::ate);
 require(bool(f),"shader file not found");const auto n=f.tellg();require(n>0&&n%4==0,"invalid shader length");
 std::vector<uint32_t> bytes(static_cast<size_t>(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),n);require(bool(f),"shader read failed");return bytes;
}
void image(const std::filesystem::path& path,const uint32_t* pixels,uint32_t w,uint32_t h){
 std::ofstream f(path,std::ios::binary);f<<"P6\n"<<w<<' '<<h<<"\n255\n";
 for(size_t i=0;i<size_t(w)*h;++i){const char rgb[]={char(pixels[i]&255),char((pixels[i]>>8)&255),char((pixels[i]>>16)&255)};f.write(rgb,3);}require(bool(f),"image write failed");
}
struct Owner {
 gpu::Device* gpu_device=nullptr;gpu::CommandPool* pool=nullptr;gpu::TimelineSemaphore* timeline=nullptr;uint64_t serial=0;
 VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;
 VkInstance instance=VK_NULL_HANDLE;VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger=nullptr;bool validation_enabled=false;
 PFN_vkCreateAccelerationStructureKHR create_as=nullptr;PFN_vkDestroyAccelerationStructureKHR destroy_as=nullptr;
 PFN_vkGetAccelerationStructureBuildSizesKHR sizes=nullptr;PFN_vkCmdBuildAccelerationStructuresKHR build=nullptr;
 PFN_vkGetAccelerationStructureDeviceAddressKHR address=nullptr;
 ~Owner(){if(gpu_device){gpu::wait_idle(gpu_device);if(pool)gpu::destroy_command_pool(pool);if(timeline)gpu::destroy_timeline_semaphore(timeline);if(messenger)destroy_messenger(instance,messenger,nullptr);gpu::destroy_device(gpu_device);}}
 void initialize(bool require_validation=false){
#if defined(NDEBUG)
  require(!require_validation,"--validate requires a Debug build; refusing an unvalidated Release run");
#endif
  gpu::set_error_callback([](const char* m,bool)noexcept{std::cerr<<"NoGraphicsAPI: "<<m<<'\n';});
  const auto init=gpu::create_device({.enable_ray_query=true});gpu_device=init.device;
  if(!gpu_device)throw std::runtime_error("NoGraphicsAPI initialization unsupported/failed: "+std::to_string(static_cast<int>(init.error)));
  const auto n=gpu::basix_native_vulkan(gpu_device);device=static_cast<VkDevice>(n.device);physical=static_cast<VkPhysicalDevice>(n.physical_device);instance=static_cast<VkInstance>(n.instance);validation_enabled=n.validation_enabled;
  require(!require_validation||validation_enabled,"Khronos validation is required but was not enabled. Set VK_LAYER_PATH to the verified package.");
  if(validation_enabled){
   auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));destroy_messenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));require(create&&destroy_messenger,"validation debug-utils entry points unavailable");
   VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};mi.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;mi.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;mi.pfnUserCallback=validation_callback;vkcheck(create(instance,&mi,nullptr,&messenger),"create validation counter");
  }
  pool=gpu::create_command_pool(gpu_device);timeline=gpu::create_timeline_semaphore(gpu_device);require(pool&&timeline,"command/timeline creation failed");
  create_as=reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device,"vkCreateAccelerationStructureKHR"));
  destroy_as=reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device,"vkDestroyAccelerationStructureKHR"));
  sizes=reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device,"vkGetAccelerationStructureBuildSizesKHR"));
  build=reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device,"vkCmdBuildAccelerationStructuresKHR"));
  address=reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device,"vkGetAccelerationStructureDeviceAddressKHR"));
  require(create_as&&destroy_as&&sizes&&build&&address,"ray-query entry points missing");
 }
 gpu::CommandBuffer* begin(){gpu::reset_command_pool(pool);return gpu::begin_commands(pool);}
 void submit(gpu::CommandBuffer* c){gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=timeline,.value=++serial};gpu::submit(gpu_device,{.commands={c},.completion=done});gpu::wait_timeline(done);}
};
struct Buffer {
 Owner& o;VkBuffer handle=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* mapped=nullptr;VkDeviceAddress address=0;VkDeviceSize bytes;
 Buffer(Owner& owner,VkDeviceSize size,VkBufferUsageFlags usage,bool host):o(owner),bytes(size){
  try{
   VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
   vkcheck(vkCreateBuffer(o.device,&info,nullptr,&handle),"create buffer");VkMemoryRequirements req{};vkGetBufferMemoryRequirements(o.device,handle,&req);
   VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(o.physical,&props);uint32_t type=UINT32_MAX;
   const auto flags=host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
   require(type!=UINT32_MAX,"no suitable memory type");VkMemoryAllocateFlagsInfo af{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};af.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
   VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&af;ai.allocationSize=req.size;ai.memoryTypeIndex=type;
   vkcheck(vkAllocateMemory(o.device,&ai,nullptr,&memory),"allocate memory");vkcheck(vkBindBufferMemory(o.device,handle,memory,0),"bind memory");
   if(host)vkcheck(vkMapMemory(o.device,memory,0,size,0,&mapped),"map memory");VkBufferDeviceAddressInfo di{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};di.buffer=handle;address=vkGetBufferDeviceAddress(o.device,&di);require(address!=0,"zero buffer address");
  }catch(...){release();throw;}
 }
 void release(){if(mapped)vkUnmapMemory(o.device,memory);if(handle)vkDestroyBuffer(o.device,handle,nullptr);if(memory)vkFreeMemory(o.device,memory,nullptr);}
 ~Buffer(){release();}Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
};
void barrier(VkCommandBuffer c,VkPipelineStageFlags src,VkAccessFlags sa,VkPipelineStageFlags dst,VkAccessFlags da){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=sa;b.dstAccessMask=da;vkCmdPipelineBarrier(c,src,dst,0,1,&b,0,nullptr,0,nullptr);}
struct Accel {
 Owner& o;VkAccelerationStructureKHR handle=VK_NULL_HANDLE;std::unique_ptr<Buffer> storage;
 VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
 Accel(Owner& owner,VkAccelerationStructureTypeKHR type,VkDeviceSize size):o(owner){storage=std::make_unique<Buffer>(o,size,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,false);VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};ci.buffer=storage->handle;ci.size=size;ci.type=type;vkcheck(o.create_as(o.device,&ci,nullptr,&handle),"create AS");ai.accelerationStructure=handle;}
 ~Accel(){if(handle)o.destroy_as(o.device,handle,nullptr);}Accel(const Accel&)=delete;Accel& operator=(const Accel&)=delete;
 VkDeviceAddress address(){return o.address(o.device,&ai);}
};
void pointer_smoke(Owner& o,const std::filesystem::path& out){
 struct Resources{gpu::GpuHeap heap{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(heap.owner)gpu::destroy_gpu_heap(heap);}} resources;
 constexpr uint32_t w=64,h=64;resources.heap=gpu::create_gpu_heap(o.gpu_device,16+w*h*4);const auto& heap=resources.heap;require(heap.owner&&heap.range.cpu,"mapped GPU heap unavailable");
 struct alignas(16) Root{uint64_t output;uint32_t width,height;};static_assert(sizeof(Root)==16&&offsetof(Root,width)==8);
 auto* root=reinterpret_cast<Root*>(heap.range.cpu);root->output=reinterpret_cast<uint64_t>(heap.range.gpu)+16;root->width=w;root->height=h;
 auto* pixels=reinterpret_cast<uint32_t*>(heap.range.cpu+16);std::memset(pixels,0,w*h*4);
 const auto code=shader("pointer_smoke.spv");resources.pso=gpu::create_compute_pso(o.gpu_device,{.code={reinterpret_cast<const gpu::byte*>(code.data()),code.size()*4},.entry_point="computeMain",.threadgroup_size={8,8,1}});auto* pso=resources.pso;
 require(pso!=nullptr,"pointer compute pipeline failed");
 auto* c=o.begin();gpu::bind_pso(c,pso);gpu::dispatch(c,heap.range.gpu,{8,8,1});
 barrier(static_cast<VkCommandBuffer>(gpu::basix_native_commands(c)),VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);o.submit(c);
 bool ok=true;for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)ok&=pixels[y*w+x]==((x*255/(w-1))|((y*255/(h-1))<<8)|(64u<<16)|(255u<<24));
 if(ok)image(out/"pointer-smoke.ppm",pixels,w,h);require(ok,"GPU pointer-root output disagrees with CPU oracle");
}
struct Pipeline {
 Owner& o;VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;
 explicit Pipeline(Owner& owner):o(owner){}
 ~Pipeline(){if(pipeline)vkDestroyPipeline(o.device,pipeline,nullptr);if(pool)vkDestroyDescriptorPool(o.device,pool,nullptr);if(layout)vkDestroyPipelineLayout(o.device,layout,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(o.device,set_layout,nullptr);}
 void initialize(Accel& scene,Buffer& output){
  VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=2;sl.pBindings=bindings;vkcheck(vkCreateDescriptorSetLayout(o.device,&sl,nullptr,&set_layout),"set layout");
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;vkcheck(vkCreatePipelineLayout(o.device,&pl,nullptr,&layout),"pipeline layout");
  const auto bytes=shader("ray_query.spv");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes.size()*4;sm.pCode=bytes.data();VkShaderModule module=nullptr;vkcheck(vkCreateShaderModule(o.device,&sm,nullptr,&module),"shader module");
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="computeMain";ci.layout=layout;
  const auto result=vkCreateComputePipelines(o.device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);vkDestroyShaderModule(o.device,module,nullptr);vkcheck(result,"ray query pipeline");
  const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;vkcheck(vkCreateDescriptorPool(o.device,&dp,nullptr,&pool),"descriptor pool");
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&set_layout;vkcheck(vkAllocateDescriptorSets(o.device,&da,&set),"descriptor allocate");
  VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};as.accelerationStructureCount=1;as.pAccelerationStructures=&scene.handle;
  VkDescriptorBufferInfo bi{output.handle,0,output.bytes};VkWriteDescriptorSet writes[2]{};for(auto& write:writes){write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1;}
  writes[0].pNext=&as;writes[0].dstBinding=0;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&bi;vkUpdateDescriptorSets(o.device,2,writes,0,nullptr);
 }
};
bool analytic_hit(float x,float y,float shift){const float px=x-shift;return y>=-0.7f&&y<=0.8f&&std::abs(px)<=(0.8f-y)*(0.8f/1.5f);}
struct NativeTexturePipeline {
 Owner& o;VkImageView view=nullptr;VkDescriptorSetLayout set_layout=nullptr;VkPipelineLayout layout=nullptr;VkPipeline pipeline=nullptr;VkDescriptorPool pool=nullptr;VkDescriptorSet set=nullptr;
 explicit NativeTexturePipeline(Owner& owner):o(owner){}
 ~NativeTexturePipeline(){if(pipeline)vkDestroyPipeline(o.device,pipeline,nullptr);if(pool)vkDestroyDescriptorPool(o.device,pool,nullptr);if(layout)vkDestroyPipelineLayout(o.device,layout,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(o.device,set_layout,nullptr);if(view)vkDestroyImageView(o.device,view,nullptr);}
 void initialize(VkImage image){
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=VK_FORMAT_R8G8B8A8_UNORM;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkcheck(vkCreateImageView(o.device,&vi,nullptr,&view),"external texture view");
  VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=1;sl.pBindings=&binding;vkcheck(vkCreateDescriptorSetLayout(o.device,&sl,nullptr,&set_layout),"external set layout");
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;vkcheck(vkCreatePipelineLayout(o.device,&pl,nullptr,&layout),"external pipeline layout");
  const auto bytes=shader("native_texture.spv");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes.size()*4;sm.pCode=bytes.data();VkShaderModule module=nullptr;vkcheck(vkCreateShaderModule(o.device,&sm,nullptr,&module),"external shader");
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="computeMain";ci.layout=layout;const auto result=vkCreateComputePipelines(o.device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);vkDestroyShaderModule(o.device,module,nullptr);vkcheck(result,"external pipeline");
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=1;dp.pPoolSizes=&size;vkcheck(vkCreateDescriptorPool(o.device,&dp,nullptr,&pool),"external descriptor pool");
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&set_layout;vkcheck(vkAllocateDescriptorSets(o.device,&da,&set),"external descriptor allocation");VkDescriptorImageInfo ii{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;write.pImageInfo=&ii;vkUpdateDescriptorSets(o.device,1,&write,0,nullptr);
 }
};
struct TrackedImage {
 VkImage image;VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
 void transition(VkCommandBuffer c,VkImageLayout expected,VkImageLayout next,VkPipelineStageFlags2 src,VkAccessFlags2 sa,VkPipelineStageFlags2 dst,VkAccessFlags2 da){
  require(layout==expected,"external image layout contract mismatch");VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};b.srcStageMask=src;b.srcAccessMask=sa;b.dstStageMask=dst;b.dstAccessMask=da;b.oldLayout=expected;b.newLayout=next;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};info.imageMemoryBarrierCount=1;info.pImageMemoryBarriers=&b;vkCmdPipelineBarrier2(c,&info);layout=next;
 }
};
unsigned contract_rejections=0;
class BorrowTicket {
 friend class TextureRegistry;
 gpu::NativeVulkanTexture texture_{};uint64_t epoch_=0;
 public: BorrowTicket()=default;
};
class TextureRegistry {
 VkDevice device_;gpu::NativeVulkanTexture active_{};uint64_t epoch_=0,last_use_=0;
 public:
 explicit TextureRegistry(VkDevice device):device_(device){}
 bool accepts(const gpu::NativeVulkanTexture& t,uint32_t w,uint32_t h)const{
  const auto usage=static_cast<uint32_t>(gpu::TextureUsage::storage|gpu::TextureUsage::transfer_source);
  return t.device==device_&&t.image!=0&&t.width==w&&t.height==h&&w>0&&h>0&&t.depth==1&&t.layers==1&&t.format==VK_FORMAT_R8G8B8A8_UNORM&&(t.usage&usage)==usage;
 }
 BorrowTicket bind(const gpu::NativeVulkanTexture& texture,uint32_t w,uint32_t h){
  require(active_.image==0,"cannot replace an active borrowed image");require(accepts(texture,w,h),"native texture metadata contract failed");active_=texture;last_use_=0;BorrowTicket ticket;ticket.texture_=texture;ticket.epoch_=++epoch_;return ticket;
 }
 bool valid(const BorrowTicket& ticket)const{return active_.image!=0&&ticket.epoch_==epoch_&&ticket.texture_.image==active_.image&&ticket.texture_.device==active_.device&&ticket.texture_.width==active_.width&&ticket.texture_.height==active_.height&&ticket.texture_.format==active_.format&&ticket.texture_.usage==active_.usage;}
 gpu::NativeVulkanTexture resolve(const BorrowTicket& ticket)const{require(valid(ticket),"stale or foreign texture borrow rejected");return ticket.texture_;}
 void mark_submitted(const BorrowTicket& ticket,uint64_t serial){require(valid(ticket)&&serial>0,"invalid submitted texture borrow");last_use_=serial;}
 void retire(const BorrowTicket& ticket,uint64_t completed){require(valid(ticket)&&last_use_>0&&completed>=last_use_,"texture retirement precedes GPU completion");active_={};++epoch_;last_use_=0;}
};
unsigned texture_interop(Owner& o,const std::filesystem::path& out,TextureRegistry& registry,BorrowTicket& stale){
 unsigned checked=0;
 for(const auto extent:std::array<gpu::uint32x2,3>{{{64,64},{17,9},{1,1}}}){
  struct Resources{gpu::TextureHeap heap{};gpu::Texture* image=nullptr;gpu::TextureDescriptorHeap* descriptors=nullptr;gpu::GpuHeap roots{};gpu::PSO* pso=nullptr;~Resources(){if(pso)gpu::destroy_pso(pso);if(descriptors)gpu::destroy_texture_descriptor_heap(descriptors);if(image)gpu::destroy_texture(image);if(heap.owner)gpu::destroy_texture_heap(heap);if(roots.owner)gpu::destroy_gpu_heap(roots);}} resources;
  const uint32_t w=extent.x,h=extent.y;const gpu::TextureDesc desc{.extent={w,h,1},.format=gpu::Format::rgba8_unorm,.usage=gpu::TextureUsage::storage|gpu::TextureUsage::transfer_source};const auto size=gpu::get_texture_size_align(o.gpu_device,desc);resources.heap=gpu::create_texture_heap(o.gpu_device,size.size);require(resources.heap.owner,"texture heap creation failed");resources.image=gpu::create_texture(o.gpu_device,desc,resources.heap,0);require(resources.image!=nullptr,"texture creation failed");resources.descriptors=gpu::create_texture_descriptor_heap(o.gpu_device,1);require(resources.descriptors!=nullptr,"texture descriptors unavailable");gpu::write_texture_descriptor(resources.descriptors,0,resources.image,gpu::TextureDescriptorType::storage);
  const auto metadata=gpu::basix_native_texture(resources.image);const auto ticket=registry.bind(metadata,w,h);const auto native=registry.resolve(ticket);
  require(!registry.valid(stale),"retired texture ticket accepted, including across cycles");++contract_rejections;
  auto wrong=metadata;wrong.format=VK_FORMAT_UNDEFINED;require(!registry.accepts(wrong,w,h),"invalid format accepted");++contract_rejections;
  wrong=metadata;wrong.device=nullptr;require(!registry.accepts(wrong,w,h),"wrong device accepted");++contract_rejections;
  wrong=metadata;wrong.usage=static_cast<uint32_t>(gpu::TextureUsage::storage);require(!registry.accepts(wrong,w,h),"missing transfer-source usage accepted");++contract_rejections;
  require(!registry.accepts(metadata,w+1,h),"unexpected extent accepted");++contract_rejections;
  resources.roots=gpu::create_gpu_heap(o.gpu_device,64+uint64_t(w)*h*4);require(resources.roots.owner&&resources.roots.range.cpu,"texture root memory unavailable");struct alignas(16) Root{uint64_t output;uint32_t index,width,height,mode,pad0,pad1;};static_assert(sizeof(Root)==32&&offsetof(Root,mode)==20);auto* roots=reinterpret_cast<Root*>(resources.roots.range.cpu);roots[0]={reinterpret_cast<uint64_t>(resources.roots.range.gpu)+64,0,w,h,0,0,0};roots[1]=roots[0];roots[1].mode=1;
  const auto bytes=shader("texture_smoke.spv");resources.pso=gpu::create_compute_pso(o.gpu_device,{.code={reinterpret_cast<const gpu::byte*>(bytes.data()),bytes.size()*4},.entry_point="computeMain",.threadgroup_size={8,8,1}});require(resources.pso!=nullptr,"texture NoGraphicsAPI pipeline failed");
  Buffer copy(o,uint64_t(w)*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);NativeTexturePipeline external(o);external.initialize(reinterpret_cast<VkImage>(native.image));auto* commands=o.begin();auto c=static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands));gpu::initialize_texture(commands,resources.image);gpu::set_texture_descriptor_heap(commands,resources.descriptors);gpu::bind_pso(commands,resources.pso);gpu::dispatch(commands,resources.roots.range.gpu,{(w+7)/8,(h+7)/8,1});
  TrackedImage state{reinterpret_cast<VkImage>(native.image)};state.transition(c,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={w,h,1};vkCmdCopyImageToBuffer(c,state.image,state.layout,copy.handle,1,&region);
  state.transition(c,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_STORAGE_READ_BIT|VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);vkCmdBindPipeline(c,VK_PIPELINE_BIND_POINT_COMPUTE,external.pipeline);vkCmdBindDescriptorSets(c,VK_PIPELINE_BIND_POINT_COMPUTE,external.layout,0,1,&external.set,0,nullptr);vkCmdDispatch(c,(w+7)/8,(h+7)/8,1);
  state.transition(c,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_STORAGE_READ_BIT);require(state.layout==VK_IMAGE_LAYOUT_GENERAL,"external consumer failed to restore GENERAL");gpu::set_texture_descriptor_heap(commands,resources.descriptors);gpu::bind_pso(commands,resources.pso);gpu::dispatch(commands,resources.roots.range.gpu+32,{(w+7)/8,(h+7)/8,1});barrier(c,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);o.submit(commands);
  const auto* before=static_cast<const uint32_t*>(copy.mapped);const auto* after=reinterpret_cast<const uint32_t*>(resources.roots.range.cpu+64);
  for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=y*w+x;const uint32_t r=x*255/std::max(1u,w-1),g=y*255/std::max(1u,h-1);const uint32_t initial=r|(g<<8)|(64u<<16)|(255u<<24),changed=64u|((255-g)<<8)|(r<<16)|(255u<<24);require(before[i]==initial,"native copy disagrees with NoGraphicsAPI texture oracle");require(after[i]==changed,"restored NoGraphicsAPI state disagrees with external texture oracle");checked+=2;}
  registry.mark_submitted(ticket,o.serial);bool early_retirement_rejected=false;try{registry.retire(ticket,0);}catch(const std::runtime_error&){early_retirement_rejected=true;}require(early_retirement_rejected,"incomplete retirement accepted");++contract_rejections;
  registry.retire(ticket,gpu::timeline_completed_value(o.timeline));require(!registry.valid(ticket),"retired ticket remained valid");++contract_rejections;stale=ticket;
  if(w==64)image(out/"texture-interop.ppm",after,w,h);
 }
 return checked;
}
void ray_smoke(Owner& o,const std::filesystem::path& out){
 const float vertices[]={-0.8f,-0.7f,0,0.8f,-0.7f,0,0,0.8f,0};Buffer vb(o,sizeof(vertices),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,true);std::memcpy(vb.mapped,vertices,sizeof(vertices));
 VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geom.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;geom.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
 auto& tri=geom.geometry.triangles;tri.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;tri.vertexData.deviceAddress=vb.address;tri.vertexStride=12;tri.maxVertex=2;tri.indexType=VK_INDEX_TYPE_NONE_KHR;
 VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geom;
 const uint32_t count=1;VkAccelerationStructureBuildSizesInfoKHR bs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};o.sizes(o.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&bs);
 Accel blas(o,build.type,bs.accelerationStructureSize);VkPhysicalDeviceAccelerationStructurePropertiesKHR ap{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&ap;vkGetPhysicalDeviceProperties2(o.physical,&props);
 const auto align=ap.minAccelerationStructureScratchOffsetAlignment;
 auto scratch=std::make_unique<Buffer>(o,bs.buildScratchSize+align,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);
 build.dstAccelerationStructure=blas.handle;build.scratchData.deviceAddress=(scratch->address+align-1)&~VkDeviceAddress(align-1);
 VkAccelerationStructureBuildRangeInfoKHR range{};range.primitiveCount=1;const auto* range_ptr=&range;
 auto* c=o.begin();auto native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(c));barrier(native,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);o.build(native,1,&build,&range_ptr);o.submit(c);
 Buffer instances(o,sizeof(VkAccelerationStructureInstanceKHR),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,true);
 VkAccelerationStructureInstanceKHR instance{};instance.transform.matrix[0][0]=instance.transform.matrix[1][1]=instance.transform.matrix[2][2]=1;instance.mask=255;instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;instance.accelerationStructureReference=blas.address();
 geom={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geom.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;geom.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;geom.geometry.instances.data.deviceAddress=instances.address;
 build={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geom;
 VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};o.sizes(o.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&ts);
 Accel tlas(o,build.type,ts.accelerationStructureSize);scratch=std::make_unique<Buffer>(o,ts.buildScratchSize+align,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);build.dstAccelerationStructure=tlas.handle;build.scratchData.deviceAddress=(scratch->address+align-1)&~VkDeviceAddress(align-1);
 Buffer results(o,64*64*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);Pipeline pipeline(o);pipeline.initialize(tlas,results);
 for(int frame=0;frame<2;++frame){
  const float shift=frame==0?0.0f:0.6f;instance.transform.matrix[0][3]=shift;std::memcpy(instances.mapped,&instance,sizeof(instance));std::memset(results.mapped,0,64*64*sizeof(float));
  c=o.begin();native=static_cast<VkCommandBuffer>(gpu::basix_native_commands(c));barrier(native,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  barrier(native,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);o.build(native,1,&build,&range_ptr);
  barrier(native,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  vkCmdBindPipeline(native,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);vkCmdBindDescriptorSets(native,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&pipeline.set,0,nullptr);vkCmdDispatch(native,8,8,1);
  barrier(native,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);o.submit(c);
  std::array<uint32_t,4096> pixels{};const auto* distances=static_cast<const float*>(results.mapped);size_t hits=0;
  for(uint32_t y=0;y<64;++y)for(uint32_t x=0;x<64;++x){const auto i=y*64+x;const bool expected=analytic_hit((float(x)+.5f)/32-1,(float(y)+.5f)/32-1,shift);require(expected?(std::abs(distances[i]-2)<1e-5f):(distances[i]==-1),"ray result disagrees with independent triangle oracle");if(expected)++hits;pixels[i]=expected?0xffa5e366u:0xff352013u;}
  require(hits>300&&hits<1600,"invalid triangle hit coverage");image(out/(frame==0?"ray-static.ppm":"ray-moved.ppm"),pixels.data(),64,64);
 }
}
}
int main(int argc,char** argv){
 try{
  std::filesystem::path out="gpu-output";bool validate=false;unsigned cycles=1;
  for(int i=1;i<argc;++i){
   const std::string arg=argv[i];
   if(arg=="--validate")validate=true;
   else if(arg=="--shader-dir"&&i+1<argc)shader_directory=argv[++i];
   else if(arg=="--output"&&i+1<argc)out=argv[++i];
   else if(arg=="--cycles"&&i+1<argc){const std::string n=argv[++i];const auto parsed=std::from_chars(n.data(),n.data()+n.size(),cycles);require(parsed.ec==std::errc()&&parsed.ptr==n.data()+n.size()&&cycles>0&&cycles<=16,"--cycles must be an integer from 1 to 16");}
   else throw std::runtime_error("Usage: basix_gpu_spike [--output DIRECTORY] [--validate] [--cycles 1..16]");
  }
  std::filesystem::create_directories(out);Owner owner;owner.initialize(validate);std::cout<<"Device: "<<gpu::get_device_caps(owner.gpu_device).device_name<<'\n';
  TextureRegistry texture_registry(owner.device);BorrowTicket stale_texture;unsigned texture_checks=0;for(unsigned i=0;i<cycles;++i){pointer_smoke(owner,out);ray_smoke(owner,out);texture_checks+=texture_interop(owner,out,texture_registry,stale_texture);}
  require(validation_errors==0&&validation_warnings==0,"validation emitted warning/error diagnostics");
  VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(owner.physical,&properties);
  std::ofstream receipt(out/"result.json");receipt<<"{\"status\":\"passed\",\"device\":"<<std::quoted(properties.deviceName)<<",\"driver_version_raw\":"<<properties.driverVersion<<",\"api_version_raw\":"<<properties.apiVersion<<",\"pointer_pixels\":"<<4096*cycles<<",\"ray_checks\":"<<8192*cycles<<",\"frames\":"<<2*cycles<<",\"cycles\":"<<cycles<<",\"texture_pixel_checks\":"<<texture_checks<<",\"contract_rejections\":"<<contract_rejections<<",\"validation_counter_scope\":\"after initialization through pre-device-teardown; harness scans whole-process logs\",\"texture_extents\":[[64,64],[17,9],[1,1]],\"validation_enabled\":"<<(owner.validation_enabled?"true":"false")<<",\"validation_errors\":"<<validation_errors.load()<<",\"validation_warnings\":"<<validation_warnings.load()<<",\"timing_scope\":\"correctness only; not a rendering benchmark\",\"scope\":\"same-device pointer, hardware ray query and native texture/state roundtrip; not DLSS or DXR2\"}\n";require(bool(receipt),"receipt write failed");
  std::cout<<"PASS: "<<cycles<<" cycles, "<<8192*cycles<<" ray checks, "<<texture_checks<<" texture pixel checks; validation "<<(owner.validation_enabled?"enabled":"disabled")<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
