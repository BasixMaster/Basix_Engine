// SPDX-License-Identifier: MIT
#pragma once

// This optional adapter requires locally supplied Streamline and Vulkan headers.
// The SDK and its binaries retain their own licenses and are not vendored here.
#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>
#include <sl.h>
#include <sl_dlss.h>
#include <sl_helpers_vk.h>

namespace basix {

class StreamlineRuntime final {
public:
    struct Api {
        PFun_slInit* slInit{};
        PFun_slShutdown* slShutdown{};
        PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
        PFun_slSetVulkanInfo* slSetVulkanInfo{};
        PFun_slIsFeatureSupported* slIsFeatureSupported{};
        PFun_slGetFeatureFunction* slGetFeatureFunction{};
        PFun_slGetNewFrameToken* slGetNewFrameToken{};
        PFun_slSetConstants* slSetConstants{};
        PFun_slSetTagForFrame* slSetTagForFrame{};
        PFun_slEvaluateFeature* slEvaluateFeature{};
        PFun_slFreeResources* slFreeResources{};
    };

    // Loads only an absolute, canonical SDK release directory, verifies the
    // interposer's Authenticode trust and NVIDIA signer, then initializes SR.
    // Throws on failure. Create this BEFORE the Vulkan instance/device.
    explicit StreamlineRuntime(const std::filesystem::path& release_directory);
    ~StreamlineRuntime();
    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;
    StreamlineRuntime(StreamlineRuntime&&) = delete;
    StreamlineRuntime& operator=(StreamlineRuntime&&) = delete;

    const Api& api() const noexcept { return api_; }
    bool initialized() const noexcept { return initialized_; }
    const std::filesystem::path& directory() const noexcept { return directory_; }
    sl::Result requirements(sl::FeatureRequirements& out) const;
    sl::Result bind_vulkan(const sl::VulkanInfo& info) const;
    sl::Result supports(VkPhysicalDevice physical_device) const;

    // Resolve feature entry points after bind_vulkan succeeds. Calling the SDK's
    // inline helpers directly would introduce a link-time slGetFeatureFunction.
    template<class Function> Function* feature_function(const char* name) const {
        if (!initialized_) throw std::runtime_error("Streamline is not initialized");
        void* address{};
        const auto result = api_.slGetFeatureFunction(sl::kFeatureDLSS, name, address);
        if (result != sl::Result::eOk || !address)
            throw std::runtime_error(std::string("Streamline feature lookup failed: ") + name +
                                     " result=" + std::to_string(static_cast<unsigned>(result)));
        return reinterpret_cast<Function*>(address);
    }

    // Caller MUST wait for GPU use and free viewport resources first. Invoke
    // before destroying VkDevice/VkInstance; destructor provides a final fallback.
    sl::Result shutdown() noexcept;
    static constexpr const char* project_id = "07da1096-cc02-4adf-a495-0fca95761a31";
    static constexpr const char* engine_version = "0.1.0";

private:
    Api api_{};
    std::filesystem::path directory_;
    std::wstring plugin_directory_;
    const wchar_t* plugin_paths_[1]{};
    sl::Feature features_[1]{sl::kFeatureDLSS};
    void* module_{};
    void* verified_file_{};
    bool initialized_{};
};

} // namespace basix
