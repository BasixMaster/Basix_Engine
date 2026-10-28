// SPDX-License-Identifier: MIT
// Capability queries only: no device creation, shader dispatch or AS build.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::atomic<unsigned> warnings{0},errors{0},reviewed_loader_messages{0};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void check(VkResult r,const char* message){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(message)+" VkResult="+std::to_string(r));}
VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
 const std::string message=data&&data->pMessage?data->pMessage:"";
 const std::string id=data&&data->pMessageIdName?data->pMessageIdName:"";
 const bool loader_policy=id=="Loader Message" && severity==VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT &&
  ((message.starts_with("Layer name GalaxyOverlayVkLayer") && message.ends_with("does not conform to naming standard (Policy #LLP_LAYER_3)")) ||
   (message.starts_with("Layer \"") && message.ends_with("forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'.")));
 if(loader_policy){++reviewed_loader_messages;return VK_FALSE;}
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;else if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)++warnings;
 if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)std::cerr<<"Basix capability validation: "<<data->pMessage<<'\n';return VK_FALSE;
}
template<class T,class F> std::vector<T> enumerate(F function){
 for(unsigned retry=0;retry<4;++retry){uint32_t count=0;check(function(&count,nullptr),"enumeration count");require(count<=65536,"Enumeration limit exceeded");std::vector<T> data(count);auto result=function(&count,data.data());if(result==VK_INCOMPLETE)continue;check(result,"enumeration data");data.resize(count);return data;}throw std::runtime_error("Unstable enumeration");
}
bool has(const std::vector<VkExtensionProperties>& extensions,const char* name){return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::string(e.extensionName)==name;});}
template<class T>void field_value(std::ostream& out,bool& first,const char* name,T value){if(!first)out<<',';first=false;out<<std::quoted(name)<<':'<<static_cast<uint64_t>(value);}
template<class F>void entry(std::ostream& out,const std::vector<VkExtensionProperties>& extensions,const char* name,F query){
 extern bool first_entry;if(!first_entry)out<<',';first_entry=false;
 const auto found=std::find_if(extensions.begin(),extensions.end(),[&](const auto& e){return std::string(e.extensionName)==name;});
 const bool advertised=found!=extensions.end();
 out<<"{\"extension\":"<<std::quoted(name)<<",\"advertised\":"<<(advertised?"true":"false")<<",\"spec_version\":"<<(advertised?found->specVersion:0)<<",\"feature_queried\":"<<(advertised?"true":"false")<<",\"device_enabled\":false,\"gpu_oracle_tested\":false,\"performance_measured\":false";
 if(advertised)query();else out<<",\"features\":null,\"properties\":null";
 out<<'}';
}
bool first_entry=true;
struct Instance{
 VkInstance value=nullptr;VkDebugUtilsMessengerEXT messenger=nullptr;PFN_vkDestroyDebugUtilsMessengerEXT destroy=nullptr;
 ~Instance(){if(messenger)destroy(value,messenger,nullptr);if(value)vkDestroyInstance(value,nullptr);}
};
}
int main(int argc,char** argv){
 try{
  bool validate=false;std::filesystem::path path="vulkan-capabilities.json";
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--validate")validate=true;else if(arg=="--output"&&i+1<argc)path=argv[++i];else throw std::runtime_error("Usage: basix_vulkan_caps [--validate] [--output FILE]");}
  uint32_t loader=VK_API_VERSION_1_0;check(vkEnumerateInstanceVersion(&loader),"loader version");require(loader>=VK_API_VERSION_1_1,"Vulkan1.1 required");
  std::ostringstream out;out<<R"({"schema_version":1,"checked_on":"2026-10-28","scope":"Enumeration and physical-device feature/property queries only; no logical device, shader, CLAS or performance test","loader_api_raw":)"<<loader<<R"(,"header_version":)"<<VK_HEADER_VERSION<<R"(,"validation_requested":)"<<(validate?"true":"false")<<R"(,"devices":[)";
  {
   Instance instance;auto ie=enumerate<VkExtensionProperties>([](uint32_t* n,VkExtensionProperties* p){return vkEnumerateInstanceExtensionProperties(nullptr,n,p);});
   VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=diagnostic;
   const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
   if(validate){auto layers=enumerate<VkLayerProperties>([](uint32_t* n,VkLayerProperties* p){return vkEnumerateInstanceLayerProperties(n,p);});require(std::any_of(layers.begin(),layers.end(),[&](const auto& p){return std::string(p.layerName)==layer;}),"Validation layer unavailable");require(has(ie,extension),"Debug utils unavailable");}
   VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Basix capability query";app.apiVersion=std::min(loader,VK_API_VERSION_1_4);
   VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;if(validate){ci.pNext=&debug;ci.enabledLayerCount=ci.enabledExtensionCount=1;ci.ppEnabledLayerNames=&layer;ci.ppEnabledExtensionNames=&extension;}check(vkCreateInstance(&ci,nullptr,&instance.value),"create capability instance");
   if(validate){auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance.value,"vkCreateDebugUtilsMessengerEXT"));instance.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance.value,"vkDestroyDebugUtilsMessengerEXT"));require(create&&instance.destroy,"Debug functions unavailable");check(create(instance.value,&debug,nullptr,&instance.messenger),"create query messenger");}
   auto devices=enumerate<VkPhysicalDevice>([&](uint32_t* n,VkPhysicalDevice* p){return vkEnumeratePhysicalDevices(instance.value,n,p);});require(!devices.empty(),"No Vulkan physical device");
   bool first_device=true;for(auto device:devices){
    if(!first_device)out<<',';first_device=false;VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(device,&properties);
    auto extensions=enumerate<VkExtensionProperties>([&](uint32_t* n,VkExtensionProperties* p){return vkEnumerateDeviceExtensionProperties(device,nullptr,n,p);});
    out<<R"({"name":)"<<std::quoted(properties.deviceName)<<R"(,"vendor_id":)"<<properties.vendorID<<R"(,"device_id":)"<<properties.deviceID<<R"(,"api_version_raw":)"<<properties.apiVersion<<R"(,"driver_version_raw":)"<<properties.driverVersion<<R"(,"nv_nvx_extensions":[)";
    bool first_nv=true;for(const auto& e:extensions)if(std::string(e.extensionName).starts_with("VK_NV")){if(!first_nv)out<<',';first_nv=false;out<<std::quoted(e.extensionName);}
    out<<R"(],"candidates":[)";first_entry=true;
  entry(out,extensions,"VK_NV_cluster_acceleration_structure",[&](){
   VkPhysicalDeviceClusterAccelerationStructureFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"clusterAccelerationStructure",f.clusterAccelerationStructure);
   out<<'}';
   VkPhysicalDeviceClusterAccelerationStructurePropertiesNV p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"maxVerticesPerCluster",p.maxVerticesPerCluster);
   field_value(out,first,"maxTrianglesPerCluster",p.maxTrianglesPerCluster);
   field_value(out,first,"clusterScratchByteAlignment",p.clusterScratchByteAlignment);
   field_value(out,first,"clusterByteAlignment",p.clusterByteAlignment);
   field_value(out,first,"clusterTemplateByteAlignment",p.clusterTemplateByteAlignment);
   field_value(out,first,"clusterBottomLevelByteAlignment",p.clusterBottomLevelByteAlignment);
   field_value(out,first,"clusterTemplateBoundsByteAlignment",p.clusterTemplateBoundsByteAlignment);
   field_value(out,first,"maxClusterGeometryIndex",p.maxClusterGeometryIndex);
   out<<'}';
  });
  entry(out,extensions,"VK_KHR_cooperative_matrix",[&](){
   VkPhysicalDeviceCooperativeMatrixFeaturesKHR f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"cooperativeMatrix",f.cooperativeMatrix);
   field_value(out,first,"cooperativeMatrixRobustBufferAccess",f.cooperativeMatrixRobustBufferAccess);
   out<<'}';
   VkPhysicalDeviceCooperativeMatrixPropertiesKHR p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"cooperativeMatrixSupportedStages",p.cooperativeMatrixSupportedStages);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_cooperative_matrix2",[&](){
   VkPhysicalDeviceCooperativeMatrix2FeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"cooperativeMatrixWorkgroupScope",f.cooperativeMatrixWorkgroupScope);
   field_value(out,first,"cooperativeMatrixFlexibleDimensions",f.cooperativeMatrixFlexibleDimensions);
   field_value(out,first,"cooperativeMatrixReductions",f.cooperativeMatrixReductions);
   field_value(out,first,"cooperativeMatrixConversions",f.cooperativeMatrixConversions);
   field_value(out,first,"cooperativeMatrixPerElementOperations",f.cooperativeMatrixPerElementOperations);
   field_value(out,first,"cooperativeMatrixTensorAddressing",f.cooperativeMatrixTensorAddressing);
   field_value(out,first,"cooperativeMatrixBlockLoads",f.cooperativeMatrixBlockLoads);
   out<<'}';
   VkPhysicalDeviceCooperativeMatrix2PropertiesNV p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_PROPERTIES_NV};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"cooperativeMatrixWorkgroupScopeMaxWorkgroupSize",p.cooperativeMatrixWorkgroupScopeMaxWorkgroupSize);
   field_value(out,first,"cooperativeMatrixFlexibleDimensionsMaxDimension",p.cooperativeMatrixFlexibleDimensionsMaxDimension);
   field_value(out,first,"cooperativeMatrixWorkgroupScopeReservedSharedMemory",p.cooperativeMatrixWorkgroupScopeReservedSharedMemory);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_cooperative_vector",[&](){
   VkPhysicalDeviceCooperativeVectorFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"cooperativeVector",f.cooperativeVector);
   field_value(out,first,"cooperativeVectorTraining",f.cooperativeVectorTraining);
   out<<'}';
   VkPhysicalDeviceCooperativeVectorPropertiesNV p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"cooperativeVectorSupportedStages",p.cooperativeVectorSupportedStages);
   field_value(out,first,"cooperativeVectorTrainingFloat16Accumulation",p.cooperativeVectorTrainingFloat16Accumulation);
   field_value(out,first,"cooperativeVectorTrainingFloat32Accumulation",p.cooperativeVectorTrainingFloat32Accumulation);
   field_value(out,first,"maxCooperativeVectorComponents",p.maxCooperativeVectorComponents);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_ray_tracing_invocation_reorder",[&](){
   VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"rayTracingInvocationReorder",f.rayTracingInvocationReorder);
   out<<'}';
   VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"rayTracingInvocationReorderReorderingHint",p.rayTracingInvocationReorderReorderingHint);
   out<<'}';
  });
  entry(out,extensions,"VK_EXT_ray_tracing_invocation_reorder",[&](){
   VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"rayTracingInvocationReorder",f.rayTracingInvocationReorder);
   out<<'}';
   VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"rayTracingInvocationReorderReorderingHint",p.rayTracingInvocationReorderReorderingHint);
   field_value(out,first,"maxShaderBindingTableRecordIndex",p.maxShaderBindingTableRecordIndex);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_partitioned_acceleration_structure",[&](){
   VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"partitionedAccelerationStructure",f.partitionedAccelerationStructure);
   out<<'}';
   VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"maxPartitionCount",p.maxPartitionCount);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_ray_tracing_linear_swept_spheres",[&](){
   VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"spheres",f.spheres);
   field_value(out,first,"linearSweptSpheres",f.linearSweptSpheres);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_device_generated_commands_compute",[&](){
   VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"deviceGeneratedCompute",f.deviceGeneratedCompute);
   field_value(out,first,"deviceGeneratedComputePipelines",f.deviceGeneratedComputePipelines);
   field_value(out,first,"deviceGeneratedComputeCaptureReplay",f.deviceGeneratedComputeCaptureReplay);
   out<<'}';
  });
  entry(out,extensions,"VK_EXT_device_generated_commands",[&](){
   VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"deviceGeneratedCommands",f.deviceGeneratedCommands);
   field_value(out,first,"dynamicGeneratedPipelineLayout",f.dynamicGeneratedPipelineLayout);
   out<<'}';
   VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"maxIndirectPipelineCount",p.maxIndirectPipelineCount);
   field_value(out,first,"maxIndirectShaderObjectCount",p.maxIndirectShaderObjectCount);
   field_value(out,first,"maxIndirectSequenceCount",p.maxIndirectSequenceCount);
   field_value(out,first,"maxIndirectCommandsTokenCount",p.maxIndirectCommandsTokenCount);
   field_value(out,first,"maxIndirectCommandsTokenOffset",p.maxIndirectCommandsTokenOffset);
   field_value(out,first,"maxIndirectCommandsIndirectStride",p.maxIndirectCommandsIndirectStride);
   field_value(out,first,"supportedIndirectCommandsInputModes",p.supportedIndirectCommandsInputModes);
   field_value(out,first,"supportedIndirectCommandsShaderStages",p.supportedIndirectCommandsShaderStages);
   field_value(out,first,"supportedIndirectCommandsShaderStagesPipelineBinding",p.supportedIndirectCommandsShaderStagesPipelineBinding);
   field_value(out,first,"supportedIndirectCommandsShaderStagesShaderBinding",p.supportedIndirectCommandsShaderStagesShaderBinding);
   field_value(out,first,"deviceGeneratedCommandsTransformFeedback",p.deviceGeneratedCommandsTransformFeedback);
   field_value(out,first,"deviceGeneratedCommandsMultiDrawIndirectCount",p.deviceGeneratedCommandsMultiDrawIndirectCount);
   out<<'}';
  });
  entry(out,extensions,"VK_KHR_compute_shader_derivatives",[&](){
   VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"computeDerivativeGroupQuads",f.computeDerivativeGroupQuads);
   field_value(out,first,"computeDerivativeGroupLinear",f.computeDerivativeGroupLinear);
   out<<'}';
   VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"meshAndTaskShaderDerivatives",p.meshAndTaskShaderDerivatives);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_compute_shader_derivatives",[&](){
   VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"computeDerivativeGroupQuads",f.computeDerivativeGroupQuads);
   field_value(out,first,"computeDerivativeGroupLinear",f.computeDerivativeGroupLinear);
   out<<'}';
  });
  entry(out,extensions,"VK_NV_memory_decompression",[&](){
   VkPhysicalDeviceMemoryDecompressionFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_EXT};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"memoryDecompression",f.memoryDecompression);
   out<<'}';
   VkPhysicalDeviceMemoryDecompressionPropertiesEXT p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_EXT};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"decompressionMethods",p.decompressionMethods);
   field_value(out,first,"maxDecompressionIndirectCount",p.maxDecompressionIndirectCount);
   out<<'}';
  });
  entry(out,extensions,"VK_EXT_memory_decompression",[&](){
   VkPhysicalDeviceMemoryDecompressionFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_EXT};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"memoryDecompression",f.memoryDecompression);
   out<<'}';
   VkPhysicalDeviceMemoryDecompressionPropertiesEXT p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_EXT};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"decompressionMethods",p.decompressionMethods);
   field_value(out,first,"maxDecompressionIndirectCount",p.maxDecompressionIndirectCount);
   out<<'}';
  });
  entry(out,extensions,"VK_EXT_opacity_micromap",[&](){
   VkPhysicalDeviceOpacityMicromapFeaturesEXT f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"micromap",f.micromap);
   field_value(out,first,"micromapCaptureReplay",f.micromapCaptureReplay);
   field_value(out,first,"micromapHostCommands",f.micromapHostCommands);
   out<<'}';
   VkPhysicalDeviceOpacityMicromapPropertiesEXT p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"maxOpacity2StateSubdivisionLevel",p.maxOpacity2StateSubdivisionLevel);
   field_value(out,first,"maxOpacity4StateSubdivisionLevel",p.maxOpacity4StateSubdivisionLevel);
   out<<'}';
  });
  entry(out,extensions,"VK_KHR_opacity_micromap",[&](){
   VkPhysicalDeviceOpacityMicromapFeaturesKHR f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_KHR};VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};q.pNext=&f;vkGetPhysicalDeviceFeatures2(device,&q);out<<R"(,"features":{)";bool first=true;
   field_value(out,first,"micromap",f.micromap);
   out<<'}';
   VkPhysicalDeviceOpacityMicromapPropertiesKHR p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_KHR};VkPhysicalDeviceProperties2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};q2.pNext=&p;vkGetPhysicalDeviceProperties2(device,&q2);out<<R"(,"properties":{)";first=true;
   field_value(out,first,"maxOpacity2StateSubdivisionLevel",p.maxOpacity2StateSubdivisionLevel);
   field_value(out,first,"maxOpacity4StateSubdivisionLevel",p.maxOpacity4StateSubdivisionLevel);
   field_value(out,first,"maxOpacityLossy4StateSubdivisionLevel",p.maxOpacityLossy4StateSubdivisionLevel);
   field_value(out,first,"maxMicromapTriangles",p.maxMicromapTriangles);
   out<<'}';
  });

    out<<"]}";
   }
  }
  require(errors==0&&warnings==0,"Capability validation diagnostics emitted");
  out<<R"(],"validation_errors":)"<<errors<<R"(,"validation_warnings":)"<<warnings<<R"(,"reviewed_loader_environment_warnings":)"<<reviewed_loader_messages<<R"(,"loader_warning_scope":"Only Loader Message warning IDs for pre-existing Galaxy layer naming and layers explicitly disabled by the process filter; all other warnings/errors fail")"<<'}';
  if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<out.str()<<'\n';require(bool(file),"Capability receipt write failed");
  std::cout<<"PASS capability query only; no advanced execution capability proved\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
