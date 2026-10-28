"""Prepare a separate pinned MIT NoGraphicsAPI copy with an experimental Vulkan-only RT seam.
Never mutates the supplied upstream checkout or downloads dependencies.
"""
from pathlib import Path
import argparse,subprocess,tarfile,io
PIN='ffd9f2ab6b7442dcfbe0cb0051d84d0edb078d89'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
 src=a.source.resolve();dst=a.destination.resolve()
 if dst==src or dst.is_relative_to(src) or src.is_relative_to(dst):raise SystemExit('Source and destination must be separate trees.')
 actual=subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()
 if actual!=PIN:raise SystemExit('Upstream revision mismatch: '+actual)
 if subprocess.check_output(['git','-C',str(src),'status','--porcelain'],text=True).strip():raise SystemExit('Upstream checkout must be clean.')
 if dst.exists():raise SystemExit('Destination exists; use a new build directory.')
 # Export committed bytes only, including notices, never ignored local files.
 archive=subprocess.check_output(['git','-C',str(src),'archive','--format=tar',PIN])
 dst.mkdir(parents=True)
 with tarfile.open(fileobj=io.BytesIO(archive),mode='r:') as tar:
  for member in tar.getmembers():
   target=(dst/member.name).resolve()
   if not target.is_relative_to(dst):raise SystemExit('Archive path escaped destination.')
   if member.isdir():target.mkdir(parents=True,exist_ok=True)
   elif member.isfile():
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(tar.extractfile(member).read())
   else:raise SystemExit('Unsupported archive entry: '+member.name)
 h=dst/'include/NoGraphicsAPI/NoGraphicsAPI.hpp';s=h.read_text()
 s=s.replace('    void* window = nullptr; // HWND', '    bool enable_ray_query = false; // Basix experimental Vulkan-only device contract.\n    void* window = nullptr; // HWND',1)
 s+='\nnamespace gpu {\nstruct NativeVulkanHandles { void* instance; void* physical_device; void* device; bool validation_enabled; };\nstruct NativeVulkanTexture { void* device; uint64 image; uint32 format, width, height, depth, layers, usage; };\nNativeVulkanHandles basix_native_vulkan(Device* device) noexcept;\nvoid* basix_native_commands(CommandBuffer* commands) noexcept;\nNativeVulkanTexture basix_native_texture(Texture* texture) noexcept;\n}\n'
 s+='\nnamespace gpu { bool basix_signal_timeline(TimelineSemaphore* semaphore, uint64 value) noexcept; }\n'
 # Optional SDK requirements are borrowed only during create_device. The caller
 # queries and validates every requested feature against the selected adapter.
 s=s.replace('    bool enable_ray_query = false;', '''    const char* const* basix_instance_extensions = nullptr;
    uint32 basix_instance_extension_count = 0;
    const char* const* basix_device_extensions = nullptr;
    uint32 basix_device_extension_count = 0;
    bool (*basix_configure_features)(void* physical, void* vulkan12, void* vulkan13, void* user) noexcept = nullptr;
    void* basix_feature_user = nullptr;
    bool basix_require_shader_int64 = false;
    bool enable_ray_query = false;''',1)
 s+='\nnamespace gpu { uint32 basix_graphics_queue_family(Device* device) noexcept; }\n'
 h.write_text(s,encoding='utf-8')
 f=dst/'src/NoGraphicsAPI.cpp';s=f.read_text();needle='    const char* enabled_device_extensions[7]{};'
 if s.count(needle)!=1:raise SystemExit('Upstream patch context changed.')
 s=s.replace(needle,'''    VkPhysicalDeviceRayQueryFeaturesKHR basix_rq{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR basix_as{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    if (desc.enable_ray_query)
    {
        VkExtensionProperties basix_ext[max_device_extensions]{};
        uint32 basix_count = 0;
        const char* basix_names[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME};
        if (enumerate_device_extensions(state->physical_device, {basix_ext, max_device_extensions}, basix_count) != Error::none)
            return fail_device_creation(state, Error::unsupported, "Basix RT extension enumeration failed");
        for (const char* name : basix_names)
        {
            bool found = false;
            for (uint32 i = 0; i < basix_count; ++i) if (strcmp(name, basix_ext[i].extensionName) == 0) found = true;
            if (!found) return fail_device_creation(state, Error::unsupported, name);
        }
        basix_as.pNext = &basix_rq;
        VkPhysicalDeviceFeatures2 basix_features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &basix_as };
        vkGetPhysicalDeviceFeatures2(state->physical_device, &basix_features);
        if (!basix_as.accelerationStructure || !basix_rq.rayQuery)
            return fail_device_creation(state, Error::unsupported, "Basix requires accelerationStructure + rayQuery");
        basix_as = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .pNext = &basix_rq, .accelerationStructure = VK_TRUE };
        basix_rq = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, .pNext = enabled_features.core.pNext, .rayQuery = VK_TRUE };
        enabled_features.core.pNext = &basix_as;
    }
    const char* enabled_device_extensions[10]{};''',1)
 needle='    const VkDeviceCreateInfo device_info{'
 if s.count(needle)!=1:raise SystemExit('Device-create patch context changed.')
 s=s.replace(needle,'''    if (desc.enable_ray_query)
    {
        enabled_device_extensions[enabled_device_extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        enabled_device_extensions[enabled_device_extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        enabled_device_extensions[enabled_device_extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
    }
'''+needle,1)
 s=s.replace('    Format format = Format::rgba8_unorm;\n\n    ~Texture()', '    Format format = Format::rgba8_unorm;\n    TextureUsage basix_usage = TextureUsage::none;\n\n    ~Texture()',1)
 s=s.replace('        .format = desc.format,\n', '        .format = desc.format,\n        .basix_usage = desc.usage,\n',1)
 s=s.replace('struct Device\n{\n    VkInstance instance', 'struct Device\n{\n    bool basix_validation_enabled = false;\n    VkInstance instance',1)
 s=s.replace('    const VkInstanceCreateInfo instance_info{', '    state->basix_validation_enabled = enabled_layer_count != 0;\n    const VkInstanceCreateInfo instance_info{',1)
 s+='\nnamespace gpu {\nNativeVulkanHandles basix_native_vulkan(Device* d) noexcept { return {d->instance, d->physical_device, d->device, d->basix_validation_enabled}; }\nvoid* basix_native_commands(CommandBuffer* c) noexcept { return c->command_buffer; }\nNativeVulkanTexture basix_native_texture(Texture* t) noexcept { return {t->state->device, (uint64)t->image, (uint32)to_vk(t->format), t->width, t->height, t->depth, t->layer_count, (uint32)t->basix_usage}; }\n}\n'
 s+='\nnamespace gpu { bool basix_signal_timeline(TimelineSemaphore* t, uint64 value) noexcept { VkSemaphoreSignalInfo info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, .semaphore = t->semaphore, .value = value }; return vkSignalSemaphore(t->state->device, &info) == VK_SUCCESS; } }\n'
 s=s.replace('    const char* enabled_instance_extensions[7]{};', '    const char* enabled_instance_extensions[64]{};',1)
 s=s.replace('    state->basix_validation_enabled = enabled_layer_count != 0;', '''    if (desc.basix_instance_extension_count > 48 || (desc.basix_instance_extension_count && !desc.basix_instance_extensions))
        return fail_device_creation(state, Error::unsupported, "Invalid SDK instance extension list");
    for (uint32 i = 0; i < desc.basix_instance_extension_count; ++i)
    {
        const char* name = desc.basix_instance_extensions[i];
        if (!name || !has_name({instance_extensions, instance_extension_count}, name))
            return fail_device_creation(state, Error::unsupported, "Required SDK instance extension unavailable");
        bool present = false;
        for (uint32 j = 0; j < enabled_instance_extension_count; ++j) if (strcmp(enabled_instance_extensions[j], name) == 0) present = true;
        if (!present)
            enabled_instance_extensions[enabled_instance_extension_count++] = name;
    }
    state->basix_validation_enabled = enabled_layer_count != 0;''',1)
 s=s.replace('    const char* enabled_device_extensions[10]{};', '    const char* enabled_device_extensions[64]{};',1)
 s=s.replace('    const VkDeviceCreateInfo device_info{', '''    if (desc.basix_device_extension_count > 48 || (desc.basix_device_extension_count && !desc.basix_device_extensions))
        return fail_device_creation(state, Error::unsupported, "Invalid SDK device extension list");
    if (desc.basix_device_extension_count)
    {
        VkExtensionProperties basix_sdk_ext[max_device_extensions]{};
        uint32 basix_sdk_count = 0;
        if (enumerate_device_extensions(state->physical_device, {basix_sdk_ext, max_device_extensions}, basix_sdk_count) != Error::none)
            return fail_device_creation(state, Error::unsupported, "SDK extension enumeration failed");
        for (uint32 i = 0; i < desc.basix_device_extension_count; ++i)
        {
            const char* name = desc.basix_device_extensions[i];
            if (!name || !has_name({basix_sdk_ext, basix_sdk_count}, name))
                return fail_device_creation(state, Error::unsupported, "Required SDK device extension unavailable");
            bool present = false;
            for (uint32 j = 0; j < enabled_device_extension_count; ++j) if (strcmp(enabled_device_extensions[j], name) == 0) present = true;
            if (!present)
                enabled_device_extensions[enabled_device_extension_count++] = name;
        }
    }
    if (desc.basix_configure_features && !desc.basix_configure_features(state->physical_device, &enabled_features.vulkan12, &enabled_features.vulkan13, desc.basix_feature_user))
        return fail_device_creation(state, Error::unsupported, "Required SDK Vulkan features unavailable");
    for (uint32 i = 0; i < desc.basix_device_extension_count; ++i)
    {
        if (strcmp(desc.basix_device_extensions[i], VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
        {
            VkPhysicalDeviceVulkan14Features sdk14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
            VkPhysicalDeviceFeatures2 sdk_features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &sdk14 };
            vkGetPhysicalDeviceFeatures2(state->physical_device, &sdk_features);
            if (!sdk14.pushDescriptor) return fail_device_creation(state, Error::unsupported, "SDK requires core1.4 pushDescriptor");
            enabled_features.vulkan14.pushDescriptor = VK_TRUE;
        }
    }
    if (desc.basix_require_shader_int64)
    {
        VkPhysicalDeviceFeatures available{};
        vkGetPhysicalDeviceFeatures(state->physical_device, &available);
        if (!available.shaderInt64) return fail_device_creation(state, Error::unsupported, "Basix PT requires shaderInt64");
        enabled_features.core.features.shaderInt64 = VK_TRUE;
    }
    const VkDeviceCreateInfo device_info{''',1)
 s+='\nnamespace gpu { uint32 basix_graphics_queue_family(Device* d) noexcept { return d->queues[0].family_index; } }\n'
 f.write_text(s,encoding='utf-8');(dst/'BASIX_INTEROP_REVISION').write_text(PIN+'\n');(dst/'BASIX_INTEROP_PATCH_VERSION').write_text('6\n')
 print('Prepared experimental Vulkan RT interop from '+PIN)
if __name__=='__main__':main()
