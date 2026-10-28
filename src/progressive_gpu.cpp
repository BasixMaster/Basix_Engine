// SPDX-License-Identifier: MIT
// Progressive Vulkan reference: NoGraphicsAPI owns device, command pools and timelines.
// Raw Vulkan lives only in this explicit interop scope. This is not a complete renderer.
#include "basix/reference.hpp"
#include "basix/progressive_gpu_abi.hpp"
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <limits>
#include <numbers>
#include <map>
#include <bit>
#include <chrono>
#include <set>
#include <sstream>
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
  const auto init=gpu::create_device({.basix_require_shader_int64=true,.enable_ray_query=true});gpu_device=init.device;
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
 void wait(uint64_t value){gpu::wait_timeline({.semaphore=timeline,.value=value});}
 uint64_t submit_async(gpu::CommandBuffer* c,gpu::TimelineSemaphore* gate){gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=timeline,.value=++serial};const gpu::TimelinePoint hold{.semaphore=gate,.value=1};gpu::submit(gpu_device,{.commands={c},.waits={hold},.completion=done});return serial;}
 void submit(gpu::CommandBuffer* c){gpu::end_commands(c);const gpu::TimelinePoint done{.semaphore=timeline,.value=++serial};gpu::submit(gpu_device,{.commands={c},.completion=done});wait(serial);}
};
// Declare after all resources referenced by the held submission: unwind must
// release the queue gate and drain before those resources are destroyed.
struct SubmissionGate {
 Owner& owner;gpu::TimelineSemaphore* semaphore=nullptr;bool released=false;
 explicit SubmissionGate(Owner& o):owner(o){semaphore=gpu::create_timeline_semaphore(o.gpu_device);require(semaphore!=nullptr,"submission gate creation failed");}
 bool release()noexcept{if(!released)released=gpu::basix_signal_timeline(semaphore,1);return released;}
 ~SubmissionGate(){release();gpu::wait_idle(owner.gpu_device);gpu::destroy_timeline_semaphore(semaphore);}
 SubmissionGate(const SubmissionGate&)=delete;SubmissionGate& operator=(const SubmissionGate&)=delete;
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
namespace br=basix::render;
namespace ref=basix::reference;
namespace abi=basix::gpu_reference;
struct NativePipeline {
 Owner& owner;VkDescriptorSetLayout set_layout=nullptr;VkPipelineLayout layout=nullptr;
 VkPipeline pipeline=nullptr;VkDescriptorPool pool=nullptr;VkDescriptorSet set=nullptr;
 explicit NativePipeline(Owner& o):owner(o){}
 ~NativePipeline(){if(pipeline)vkDestroyPipeline(owner.device,pipeline,nullptr);if(pool)vkDestroyDescriptorPool(owner.device,pool,nullptr);if(layout)vkDestroyPipelineLayout(owner.device,layout,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(owner.device,set_layout,nullptr);}
 void initialize(Accel& scene){
  VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=1;sl.pBindings=&binding;vkcheck(vkCreateDescriptorSetLayout(owner.device,&sl,nullptr,&set_layout),"PT AS set layout");
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,8};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;vkcheck(vkCreatePipelineLayout(owner.device,&pl,nullptr,&layout),"PT pointer-root pipeline layout");
  const auto bytes=shader("progressive_pt.spv");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes.size()*4;sm.pCode=bytes.data();VkShaderModule module=nullptr;vkcheck(vkCreateShaderModule(owner.device,&sm,nullptr,&module),"PT shader module");
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="computeMain";ci.layout=layout;auto status=vkCreateComputePipelines(owner.device,nullptr,1,&ci,nullptr,&pipeline);vkDestroyShaderModule(owner.device,module,nullptr);vkcheck(status,"PT pipeline");
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=1;dp.pPoolSizes=&size;vkcheck(vkCreateDescriptorPool(owner.device,&dp,nullptr,&pool),"PT descriptor pool");
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&set_layout;vkcheck(vkAllocateDescriptorSets(owner.device,&da,&set),"PT descriptor allocation");VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};as.accelerationStructureCount=1;as.pAccelerationStructures=&scene.handle;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.pNext=&as;write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;vkUpdateDescriptorSets(owner.device,1,&write,0,nullptr);
 }
};
struct SceneAcceleration {
 Owner& o;std::unique_ptr<Buffer> vertices,instances;std::unique_ptr<Accel> blas,tlas;
 explicit SceneAcceleration(Owner& owner,const ref::Scene& scene):o(owner){
  std::vector<float> vertex_data;for(const auto& t:scene.triangles)for(auto v:{t.a,t.b,t.c})for(double x:{v.x,v.y,v.z})vertex_data.push_back(float(x));
  require(!vertex_data.empty()&&vertex_data.size()/9<=100000,"PT geometry bounds");
  vertices=std::make_unique<Buffer>(o,vertex_data.size()*sizeof(float),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,true);std::memcpy(vertices->mapped,vertex_data.data(),vertex_data.size()*sizeof(float));
  VkPhysicalDeviceAccelerationStructurePropertiesKHR ap{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&ap;vkGetPhysicalDeviceProperties2(o.physical,&properties);const auto alignment=ap.minAccelerationStructureScratchOffsetAlignment;require(alignment&&!(alignment&(alignment-1)),"Invalid AS scratch alignment");
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;auto& triangles=geometry.geometry.triangles;triangles.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;triangles.vertexData.deviceAddress=vertices->address;triangles.vertexStride=12;triangles.maxVertex=uint32_t(vertex_data.size()/3-1);triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;
  uint32_t count=uint32_t(scene.triangles.size());VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};o.sizes(o.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);blas=std::make_unique<Accel>(o,build.type,sizes.accelerationStructureSize);auto scratch=std::make_unique<Buffer>(o,sizes.buildScratchSize+alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);build.dstAccelerationStructure=blas->handle;build.scratchData.deviceAddress=(scratch->address+alignment-1)&~VkDeviceAddress(alignment-1);
  VkAccelerationStructureBuildRangeInfoKHR range{};range.primitiveCount=count;const auto* range_pointer=&range;auto* commands=o.begin();auto vk=static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands));barrier(vk,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);o.build(vk,1,&build,&range_pointer);o.submit(commands);
  instances=std::make_unique<Buffer>(o,sizeof(VkAccelerationStructureInstanceKHR),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,true);VkAccelerationStructureInstanceKHR instance{};instance.transform.matrix[0][0]=instance.transform.matrix[1][1]=instance.transform.matrix[2][2]=1;instance.mask=255;instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;instance.accelerationStructureReference=blas->address();std::memcpy(instances->mapped,&instance,sizeof(instance));
  geometry={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;geometry.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;geometry.geometry.instances.data.deviceAddress=instances->address;
  build={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;count=range.primitiveCount=1;sizes={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};o.sizes(o.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);tlas=std::make_unique<Accel>(o,build.type,sizes.accelerationStructureSize);scratch=std::make_unique<Buffer>(o,sizes.buildScratchSize+alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);build.dstAccelerationStructure=tlas->handle;build.scratchData.deviceAddress=(scratch->address+alignment-1)&~VkDeviceAddress(alignment-1);
  commands=o.begin();vk=static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands));barrier(vk,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);barrier(vk,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);o.build(vk,1,&build,&range_pointer);barrier(vk,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);o.submit(commands);
 }
};
br::Vec3 quantize(br::Vec3 v){return {float(v.x),float(v.y),float(v.z)};}
ref::Scene fixture(bool split){
 auto scene=ref::cornell_scene();scene.spheres.clear();
 uint32_t id=100;
 auto quad=[&](br::Vec3 a,br::Vec3 b,br::Vec3 c,br::Vec3 d,uint32_t material){scene.triangles.push_back({id++,material,a,b,c});scene.triangles.push_back({id++,material,a,c,d});};
 auto box=[&](br::Vec3 a,br::Vec3 b,uint32_t material){
  quad({a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},material);
  quad({b.x,a.y,b.z},{a.x,a.y,b.z},{a.x,b.y,b.z},{b.x,b.y,b.z},material);
  quad({a.x,a.y,b.z},{a.x,a.y,a.z},{a.x,b.y,a.z},{a.x,b.y,b.z},material);
  quad({b.x,a.y,a.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{b.x,b.y,a.z},material);
  quad({a.x,b.y,a.z},{b.x,b.y,a.z},{b.x,b.y,b.z},{a.x,b.y,b.z},material);
  quad({a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,a.y,a.z},{a.x,a.y,a.z},material);
 };
 const auto box_start=scene.triangles.size();
 box({-.72,-.99,-.05},{-.12,-.30,.50},5);box({.16,-.99,-.65},{.69,.02,-.12},4);
 for(size_t i=box_start;i<scene.triangles.size();++i)std::swap(scene.triangles[i].b,scene.triangles[i].c); // closed boxes face outward
 if(split){auto found=std::find_if(scene.triangles.begin(),scene.triangles.end(),[&](const auto& t){return t.material_id==6;});require(found!=scene.triangles.end(),"Missing emitter");const auto original=*found;const auto point=original.a+(original.b-original.a)*.2;*found={original.id,6,original.a,point,original.c};scene.triangles.push_back({id++,6,point,original.b,original.c});}
 for(auto& t:scene.triangles){t.a=quantize(t.a);t.b=quantize(t.b);t.c=quantize(t.c);}
 for(auto& m:scene.materials){m.base_color=quantize(m.base_color);m.emission=quantize(m.emission);m.roughness=float(m.roughness);m.metallic=float(m.metallic);}
 std::string reason;require(ref::validate(scene,&reason),reason.c_str());return scene;
}
struct MappedScene {
 Owner& owner;gpu::GpuHeap heap{};abi::Root* root=nullptr;abi::Float4* accumulation=nullptr;abi::Float4* hits=nullptr;uint64_t root_address{};
 std::vector<abi::Triangle> triangles;std::vector<abi::Material> materials;std::vector<uint32_t> lights;
 ~MappedScene(){if(heap.owner)gpu::destroy_gpu_heap(heap);}
 MappedScene(Owner& o,const ref::Scene& scene,ref::RenderConfig config,ref::Camera camera):owner(o){
  std::map<uint32_t,uint32_t> material_indices;
  for(const auto& m:scene.materials){material_indices[m.id]=uint32_t(materials.size());materials.push_back({float(m.base_color.x),float(m.base_color.y),float(m.base_color.z),float(m.roughness),float(m.metallic),float(m.emission.x),float(m.emission.y),float(m.emission.z),m.model==br::MaterialModel::lambert?0u:1u,0,0,0});}
  double total_area=0;
  for(const auto& t:scene.triangles){const auto cross=br::cross(t.b-t.a,t.c-t.a);const auto n=br::normalize(cross);const double area=.5*std::hypot(cross.x,cross.y,cross.z);const auto material=material_indices.at(t.material_id);triangles.push_back({float(t.a.x),float(t.a.y),float(t.a.z),material,float(t.b.x),float(t.b.y),float(t.b.z),t.id,float(t.c.x),float(t.c.y),float(t.c.z),float(area),float(n.x),float(n.y),float(n.z),0});if(br::max_component(scene.materials[material].emission)>0){lights.push_back(uint32_t(triangles.size()-1));total_area+=area;}}
  std::sort(lights.begin(),lights.end(),[&](uint32_t a,uint32_t b){return triangles[a].id<triangles[b].id;});
  auto align=[](size_t n){return (n+15)&~size_t(15);};
  size_t mo=sizeof(abi::Root),to=align(mo+materials.size()*sizeof(abi::Material)),lo=align(to+triangles.size()*sizeof(abi::Triangle)),ao=align(lo+lights.size()*sizeof(uint32_t));const size_t pixels=size_t(config.width)*config.height,ho=ao+pixels*sizeof(abi::Float4),bytes=ho+pixels*sizeof(abi::Float4);
  heap=gpu::create_gpu_heap(owner.gpu_device,bytes);if(!heap.owner||!heap.range.cpu||!heap.range.gpu){if(heap.owner)gpu::destroy_gpu_heap(heap);heap={};throw std::runtime_error("PT mapped NoGraphicsAPI heap unavailable");}root_address=reinterpret_cast<uint64_t>(heap.range.gpu);root=reinterpret_cast<abi::Root*>(heap.range.cpu);std::memset(heap.range.cpu,0,bytes);
  std::memcpy(heap.range.cpu+mo,materials.data(),materials.size()*sizeof(abi::Material));std::memcpy(heap.range.cpu+to,triangles.data(),triangles.size()*sizeof(abi::Triangle));if(!lights.empty())std::memcpy(heap.range.cpu+lo,lights.data(),lights.size()*sizeof(uint32_t));
  accumulation=reinterpret_cast<abi::Float4*>(heap.range.cpu+ao);hits=reinterpret_cast<abi::Float4*>(heap.range.cpu+ho);
  root->materials=root_address+mo;root->triangles=root_address+to;root->light_indices=root_address+lo;root->accumulation=root_address+ao;root->debug_hits=root_address+ho;root->material_count=uint32_t(materials.size());root->triangle_count=uint32_t(triangles.size());root->light_count=uint32_t(lights.size());root->width=config.width;root->height=config.height;root->max_depth=config.integrator.max_depth;root->rr_start=config.integrator.rr_start;root->flags=(config.integrator.next_event_estimation?1u:0)|(config.integrator.multiple_importance_sampling?2u:0)|(config.integrator.russian_roulette?4u:0);root->seed=config.seed;root->total_light_area=float(total_area);
  const auto forward=br::normalize(camera.target-camera.origin),right=br::normalize(br::cross(forward,camera.up)),up=br::cross(right,forward);auto v=[](br::Vec3 a,float w=0){return abi::Float4{float(a.x),float(a.y),float(a.z),w};};root->camera_origin=v(camera.origin);root->camera_forward=v(forward,float(std::tan(camera.vertical_fov_degrees*std::numbers::pi/360)));root->camera_right=v(right,float(config.width)/float(config.height));root->camera_up=v(up);root->environment=v(scene.environment);
  poison();
 }
 void poison(){const float nan=std::numeric_limits<float>::quiet_NaN();for(size_t i=0;i<size_t(root->width)*root->height;++i){accumulation[i]={nan,nan,nan,nan};hits[i]={nan,nan,nan,nan};}}
 void dispatch(NativePipeline& p,uint32_t start,uint32_t count,bool primary=false,bool omit=false){
  root->sample_start=start;root->sample_count=count;root->mode=primary?1:0;
  auto* commands=owner.begin();auto vk=static_cast<VkCommandBuffer>(gpu::basix_native_commands(commands));barrier(vk,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);barrier(vk,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  vkCmdBindPipeline(vk,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);vkCmdBindDescriptorSets(vk,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&p.set,0,nullptr);vkCmdPushConstants(vk,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,&root_address);if(!omit)vkCmdDispatch(vk,(root->width+7)/8,(root->height+7)/8,1);barrier(vk,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);owner.submit(commands);
 }
 std::vector<br::Vec3> read(uint32_t samples)const{
  std::vector<br::Vec3> image;image.reserve(size_t(root->width)*root->height);
  for(size_t i=0;i<size_t(root->width)*root->height;++i){const auto p=accumulation[i];require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&p.x>=0&&p.y>=0&&p.z>=0&&p.w==float(samples),"PT output is nonfinite, negative, untouched, or wrong sample count");image.push_back({double(p.x)/samples,double(p.y)/samples,double(p.z)/samples});}return image;
 }
};
struct Comparison {double normalized_rmse{},max_abs{},mean_relative{},low_radiance_rmse{};};
Comparison compare(const std::vector<br::Vec3>& actual,const std::vector<br::Vec3>& expected){
 require(actual.size()==expected.size()&&!actual.empty(),"PT image shape mismatch");double squared=0,energy=0,low_radiance_squared=0,low_radiance_energy=0;std::array<double,3> sums{},refs{};Comparison out;
 for(size_t i=0;i<actual.size();++i){const double a[]={actual[i].x,actual[i].y,actual[i].z},b[]={expected[i].x,expected[i].y,expected[i].z};for(unsigned c=0;c<3;++c){require(std::isfinite(a[c])&&std::isfinite(b[c])&&a[c]>=0&&b[c]>=0,"Invalid CPU/GPU image component");double error=a[c]-b[c];squared+=error*error;energy+=b[c]*b[c];if(br::luminance(expected[i])<2){low_radiance_squared+=error*error;low_radiance_energy+=b[c]*b[c];}sums[c]+=a[c];refs[c]+=b[c];out.max_abs=std::max(out.max_abs,std::abs(error));}}
 out.normalized_rmse=std::sqrt(squared/std::max(energy,1e-20));out.low_radiance_rmse=std::sqrt(low_radiance_squared/std::max(low_radiance_energy,1e-20));for(unsigned c=0;c<3;++c)out.mean_relative=std::max(out.mean_relative,std::abs(sums[c]-refs[c])/std::max(refs[c],.01*actual.size()));return out;
}
struct PrimaryResult {unsigned rays{},equivalent_ties{};};
PrimaryResult primary_oracle(MappedScene& mapped,NativePipeline& p,const ref::Scene& scene,bool omit){
 mapped.dispatch(p,0,1,true,omit);const ref::World world(scene);PrimaryResult checked;
 auto vec=[](abi::Float4 v){return br::Vec3{v.x,v.y,v.z};};const auto& r=*mapped.root;
 for(uint32_t y=0;y<r.height;++y)for(uint32_t x=0;x<r.width;++x){
  auto& actual=mapped.hits[size_t(y)*r.width+x];require(std::isfinite(actual.x)&&std::isfinite(actual.y)&&std::isfinite(actual.z)&&std::isfinite(actual.w),"Primary output untouched/nonfinite");
  const double px=(2*(double(x)+.5)/r.width-1)*r.camera_right.w*r.camera_forward.w,py=(1-2*(double(y)+.5)/r.height)*r.camera_forward.w;const auto origin=vec(r.camera_origin),direction=br::normalize(vec(r.camera_forward)+vec(r.camera_right)*px+vec(r.camera_up)*py);const auto expected=world.intersect({origin,direction});
  ++checked.rays;if(!expected){require(actual.x==-1&&actual.y==-1&&actual.z==-1&&actual.w==0,"Primary miss oracle failed");continue;}
  require(std::abs(actual.x-expected->t)<=1e-4+1e-4*std::abs(expected->t),"Primary distance oracle failed");require(actual.y>=0&&actual.y<float(scene.triangles.size())&&actual.y==std::floor(actual.y),"Primary index out of range");const auto index=uint32_t(actual.y);const auto& triangle=scene.triangles[index];require(triangle.material_id==expected->material_id&&actual.z==float(mapped.triangles[index].material)&&actual.w==(expected->front_face?1.f:0.f),"Primary material/front-face oracle failed");
  if(triangle.id!=expected->primitive_id){const auto expected_triangle=std::find_if(scene.triangles.begin(),scene.triangles.end(),[&](const auto& t){return t.id==expected->primitive_id;});require(expected_triangle!=scene.triangles.end(),"CPU primitive ID missing");const auto n=br::normalize(br::cross(triangle.b-triangle.a,triangle.c-triangle.a));const auto other=br::normalize(br::cross(expected_triangle->b-expected_triangle->a,expected_triangle->c-expected_triangle->a));std::array<br::Vec3,2> shared{};unsigned common=0;for(auto a:{triangle.a,triangle.b,triangle.c})for(auto b:{expected_triangle->a,expected_triangle->b,expected_triangle->c})if(br::dot(a-b,a-b)<1e-20){if(common<2)shared[common]=a;++common;}require(common>=2&&br::dot(n,other)>1-1e-6,"Primary non-equivalent primitive mismatch");const auto edge=shared[1]-shared[0],delta=expected->position-shared[0];const double u=br::dot(delta,edge)/br::dot(edge,edge);const auto distance=delta-edge*u;require(u>=-1e-4&&u<=1.0001&&std::hypot(distance.x,distance.y,distance.z)<=2e-4,"Primary primitive mismatch away from shared edge");++checked.equivalent_ties;}
 }
 return checked;
}
void write_float(std::ofstream& file,float value){const auto b=std::bit_cast<uint32_t>(value);const char bytes[]={char(b),char(b>>8),char(b>>16),char(b>>24)};file.write(bytes,4);}
void write_images(const std::filesystem::path& directory,const std::vector<br::Vec3>& values,uint32_t width,uint32_t height){
 std::ofstream pfm(directory/"linear.pfm",std::ios::binary),ppm(directory/"preview.ppm",std::ios::binary);pfm<<"PF\n"<<width<<' '<<height<<"\n-1.0\n";ppm<<"P6\n"<<width<<' '<<height<<"\n255\n";
 for(auto p:values)for(double c:{p.x,p.y,p.z}){c=c/(1+c);c=c<=.0031308?12.92*c:1.055*std::pow(c,1/2.4)-.055;ppm.put(char(std::lround(std::clamp(c,0.,1.)*255)));}
 for(uint32_t y=height;y>0;--y)for(uint32_t x=0;x<width;++x){auto p=values[size_t(y-1)*width+x];for(double c:{p.x,p.y,p.z})write_float(pfm,float(c));}pfm.close();ppm.close();require(bool(pfm)&&bool(ppm),"PT image export failed");
}
std::string metric(bool measured,double value){if(!measured)return "null";std::ostringstream s;s<<std::setprecision(12)<<value;return s.str();}
uint32_t integer(const char* text,uint32_t maximum){uint32_t value=0;const std::string input=text;auto result=std::from_chars(input.data(),input.data()+input.size(),value);require(result.ec==std::errc()&&result.ptr==input.data()+input.size()&&value>=1&&value<=maximum,"Invalid bounded positive integer");return value;}
}
int main(int argc,char** argv){
 try{
  std::filesystem::path output="progressive-output";ref::RenderConfig config;config.width=32;config.height=32;config.seed=20261028;config.integrator.random_bits=24;uint32_t spp=1024,batch=64;bool validate=false,omit=false,bad_epoch=false,split=true,compare_cpu=true;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];auto next=[&](){require(i+1<argc,"Missing argument value");return argv[++i];};if(arg=="--output")output=next();else if(arg=="--validate")validate=true;else if(arg=="--omit-dispatch")omit=true;else if(arg=="--bad-epoch")bad_epoch=true;else if(arg=="--unsplit-light")split=false;else if(arg=="--no-cpu-comparison")compare_cpu=false;else if(arg=="--width")config.width=integer(next(),512);else if(arg=="--height")config.height=integer(next(),512);else if(arg=="--spp")spp=integer(next(),4096);else if(arg=="--batch")batch=integer(next(),4096);else if(arg=="--depth")config.integrator.max_depth=integer(next(),16);else if(arg=="--seed")config.seed=integer(next(),UINT32_MAX);else if(arg=="--no-nee")config.integrator.next_event_estimation=false;else if(arg=="--no-mis")config.integrator.multiple_importance_sampling=false;else if(arg=="--no-rr")config.integrator.russian_roulette=false;else if(arg=="--shader-dir")shader_directory=next();else throw std::runtime_error("Unknown PT argument");}
  require(uint64_t(config.width)*config.height*spp*(uint64_t(config.integrator.max_depth)+1)<=160000000,"PT workload exceeds bounded experiment budget");require(!compare_cpu||uint64_t(config.width)*config.height*spp<=4194304,"CPU comparison budget exceeded; select smaller workload or explicit --no-cpu-comparison");std::filesystem::create_directories(output);
  const auto scene=fixture(split);ref::Camera camera;Comparison comparison;PrimaryResult primary;std::vector<br::Vec3> gpu_image;bool batch_identity=false,reset_identity=false;bool validation_enabled=false;double elapsed=0;
  {
   Owner owner;owner.initialize(validate);validation_enabled=owner.validation_enabled;SceneAcceleration acceleration(owner,scene);NativePipeline pipeline(owner);pipeline.initialize(*acceleration.tlas);MappedScene mapped(owner,scene,config,camera);
   primary=primary_oracle(mapped,pipeline,scene,false);
   const auto started=std::chrono::steady_clock::now();for(uint32_t start=0;start<spp;){const auto count=std::min(batch,spp-start);mapped.dispatch(pipeline,start==0&&bad_epoch?1:start,count,false,omit);start+=count;}gpu_image=mapped.read(spp);elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
   // Independent resets and two different partitionings preserve ordered adds.
   const uint32_t test_spp=std::min(spp,16u);mapped.poison();for(uint32_t s=0;s<test_spp;++s)mapped.dispatch(pipeline,s,1);const auto a=mapped.read(test_spp);mapped.poison();for(uint32_t s=0;s<test_spp;){const auto count=std::min(8u,test_spp-s);mapped.dispatch(pipeline,s,count);s+=count;}const auto b=mapped.read(test_spp);batch_identity=a.size()==b.size();for(size_t i=0;i<a.size();++i)batch_identity&=a[i].x==b[i].x&&a[i].y==b[i].y&&a[i].z==b[i].z;require(batch_identity,"GPU progressive batch identity failed");mapped.dispatch(pipeline,0,test_spp);const auto reset=mapped.read(test_spp);reset_identity=reset.size()==b.size();for(size_t i=0;i<b.size();++i)reset_identity&=reset[i].x==b[i].x&&reset[i].y==b[i].y&&reset[i].z==b[i].z;require(reset_identity,"GPU reset after previous accumulation failed");
  }
  if(compare_cpu){ref::ProgressiveRenderer reference(scene,camera,config);reference.add_samples(spp);comparison=compare(gpu_image,reference.linear_image());std::cout<<"CPU/GPU nRMSE="<<comparison.normalized_rmse<<" low-radiance nRMSE="<<comparison.low_radiance_rmse<<" mean="<<comparison.mean_relative<<'\n';require(comparison.normalized_rmse<=.05&&comparison.low_radiance_rmse<=.10&&comparison.mean_relative<=.02,"GPU PT image disagrees with CPU reference budget (nRMSE .05 / low-radiance .10 / channel mean .02)");}
  require(validation_errors==0&&validation_warnings==0,"PT validation diagnostics emitted");write_images(output,gpu_image,config.width,config.height);
  std::ofstream receipt(output/"result.json");receipt<<std::setprecision(12)<<"{\"status\":\"passed\",\"width\":"<<config.width<<",\"height\":"<<config.height<<",\"spp\":"<<spp<<",\"batch\":"<<batch<<",\"seed\":"<<config.seed<<",\"random_bits\":24,\"max_depth\":"<<config.integrator.max_depth<<",\"next_event_estimation\":"<<(config.integrator.next_event_estimation?"true":"false")<<",\"multiple_importance_sampling\":"<<(config.integrator.multiple_importance_sampling?"true":"false")<<",\"russian_roulette\":"<<(config.integrator.russian_roulette?"true":"false")<<",\"triangles\":"<<scene.triangles.size()<<",\"split_unequal_area_emitter\":"<<(split?"true":"false")<<",\"primary_rays\":"<<primary.rays<<",\"equivalent_shared_edge_ties\":"<<primary.equivalent_ties<<",\"batch_identity\":"<<(batch_identity?"true":"false")<<",\"reset_identity\":"<<(reset_identity?"true":"false")<<",\"cpu_comparison\":"<<(compare_cpu?"true":"false")<<",\"normalized_rmse\":"<<metric(compare_cpu,comparison.normalized_rmse)<<",\"low_radiance_normalized_rmse\":"<<metric(compare_cpu,comparison.low_radiance_rmse)<<",\"low_radiance_rmse_limit\":0.1,\"max_absolute_error\":"<<metric(compare_cpu,comparison.max_abs)<<",\"max_channel_mean_relative_error\":"<<metric(compare_cpu,comparison.mean_relative)<<",\"normalized_rmse_limit\":0.05,\"channel_mean_relative_limit\":0.02,\"validation_enabled\":"<<(validation_enabled?"true":"false")<<",\"validation_errors\":"<<validation_errors<<",\"validation_warnings\":"<<validation_warnings<<",\"dispatch_readback_elapsed_ms\":"<<elapsed<<",\"timing_scope\":\"Local synchronized dispatch/readback host elapsed; not isolated GPU time or performance comparison\",\"scope\":\"Ordinary Vulkan triangle BLAS/TLAS and compute ray-query progressive PT on NoGraphicsAPI-owned device/commands/heap; NEE/MIS/RR with optional matched24-bit CPU reference explicitly flagged. Fixed original scene; no import/textures/motion/DLSS/advanced extension or cross-device acceptance.\"}\n";receipt.close();require(bool(receipt),"PT receipt write failed");std::cout<<"PASS GPU progressive PT: "<<config.width<<'x'<<config.height<<' '<<spp<<" spp; CPU image comparison="<<(compare_cpu?"enabled":"skipped")<<'\n';return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
