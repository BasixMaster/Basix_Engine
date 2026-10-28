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
 s+='\nnamespace gpu {\nstruct NativeVulkanHandles { void* instance; void* physical_device; void* device; };\nNativeVulkanHandles basix_native_vulkan(Device* device) noexcept;\nvoid* basix_native_commands(CommandBuffer* commands) noexcept;\n}\n'
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
 s+='\nnamespace gpu {\nNativeVulkanHandles basix_native_vulkan(Device* d) noexcept { return {d->instance, d->physical_device, d->device}; }\nvoid* basix_native_commands(CommandBuffer* c) noexcept { return c->command_buffer; }\n}\n'
 f.write_text(s,encoding='utf-8');(dst/'BASIX_INTEROP_REVISION').write_text(PIN+'\n')
 print('Prepared experimental Vulkan RT interop from '+PIN)
if __name__=='__main__':main()
