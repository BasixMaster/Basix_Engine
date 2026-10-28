// SPDX-License-Identifier: MIT
#ifndef _WIN32
#error The optional Streamline dynamic runtime currently supports Windows only.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <cstdio>
#include <vector>
#include "basix/streamline_runtime.hpp"

namespace basix {
namespace {
void sdk_log(sl::LogType type, const char* message) {
    std::fprintf(stderr, "[Streamline:%u] %s\n", static_cast<unsigned>(type),
                 message ? message : "(empty message)");
}

void verify_nvidia_signature(const std::filesystem::path& dll) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = dll.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    // Reproducible local verification: do not initiate certificate-network IO.
    // A machine without the required cached trust chain fails closed.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &policy, &data);
    bool nvidia_signer = false;
    if (status == ERROR_SUCCESS) {
        auto* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        auto* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
            const auto* certificate = signer->pasCertChain[0].pCert;
            const DWORD count = CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                                   0, nullptr, nullptr, 0);
            if (count) {
                std::vector<wchar_t> name(count);
                CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                   0, nullptr, name.data(), count);
                nvidia_signer = std::wstring(name.data()) == L"NVIDIA Corporation";
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);
    if (status != ERROR_SUCCESS || !nvidia_signer)
        throw std::runtime_error("Streamline interposer signature verification failed: status=" +
                                 std::to_string(status) + " nvidia_signer=" +
                                 std::to_string(nvidia_signer));
}
} // namespace

StreamlineRuntime::StreamlineRuntime(const std::filesystem::path& release_directory) {
    if (!release_directory.is_absolute())
        throw std::invalid_argument("Streamline runtime directory must be absolute");
    directory_ = std::filesystem::canonical(release_directory);
    const auto dll = directory_ / L"sl.interposer.dll";
    // Deny writes/deletes while verification and loading take place, including
    // the module lifetime. An existing incompatible writer causes a safe failure.
    HANDLE locked_file = CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked_file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot lock Streamline interposer: Win32=" +
                                 std::to_string(GetLastError()));
    verified_file_ = locked_file;
    bool retain_failed_sdk_state = false;
    try {
        verify_nvidia_signature(dll);
        HMODULE module = LoadLibraryExW(dll.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            throw std::runtime_error("Cannot load Streamline interposer: Win32=" +
                                     std::to_string(GetLastError()));
        module_ = module;
#define BASIX_SL_RESOLVE(name) \
        api_.name = reinterpret_cast<PFun_##name*>(GetProcAddress(module, #name)); \
        if (!api_.name) throw std::runtime_error("Missing Streamline export: " #name)
        BASIX_SL_RESOLVE(slInit);
        BASIX_SL_RESOLVE(slShutdown);
        BASIX_SL_RESOLVE(slGetFeatureRequirements);
        BASIX_SL_RESOLVE(slSetVulkanInfo);
        BASIX_SL_RESOLVE(slIsFeatureSupported);
        BASIX_SL_RESOLVE(slGetFeatureFunction);
        BASIX_SL_RESOLVE(slGetNewFrameToken);
        BASIX_SL_RESOLVE(slSetConstants);
        BASIX_SL_RESOLVE(slSetTagForFrame);
        BASIX_SL_RESOLVE(slEvaluateFeature);
        BASIX_SL_RESOLVE(slFreeResources);
#undef BASIX_SL_RESOLVE
        plugin_directory_ = directory_.wstring();
        plugin_paths_[0] = plugin_directory_.c_str();
        sl::Preferences preferences{};
        preferences.pathsToPlugins = plugin_paths_;
        preferences.numPathsToPlugins = 1;
        preferences.logMessageCallback = sdk_log;
        preferences.logLevel = sl::LogLevel::eVerbose;
        preferences.showConsole = false;
        preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                            sl::PreferenceFlags::eUseManualHooking |
                            sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        preferences.featuresToLoad = features_;
        preferences.numFeaturesToLoad = 1;
        preferences.applicationId = 0;
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = engine_version;
        preferences.projectId = project_id;
        preferences.renderAPI = sl::RenderAPI::eVulkan;
        const auto result = api_.slInit(preferences, sl::kSDKVersion);
        if (result != sl::Result::eOk) {
            // Release partial SDK state if initialization failed.
            retain_failed_sdk_state = api_.slShutdown() != sl::Result::eOk;
            throw std::runtime_error("slInit failed: result=" +
                                     std::to_string(static_cast<unsigned>(result)));
        }
        initialized_ = true;
    } catch (...) {
        if (module_ && !retain_failed_sdk_state) FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
        if (!retain_failed_sdk_state) CloseHandle(static_cast<HANDLE>(verified_file_));
        verified_file_ = nullptr;
        throw;
    }
}

StreamlineRuntime::~StreamlineRuntime() {
    // A failed SDK shutdown may retain live callbacks/threads. Leave module and
    // verification lock mapped until process exit rather than unloading them.
    if (shutdown() != sl::Result::eOk) return;
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
    if (verified_file_) CloseHandle(static_cast<HANDLE>(verified_file_));
}

sl::Result StreamlineRuntime::requirements(sl::FeatureRequirements& out) const {
    return initialized_ ? api_.slGetFeatureRequirements(sl::kFeatureDLSS, out)
                        : sl::Result::eErrorNotInitialized;
}
sl::Result StreamlineRuntime::bind_vulkan(const sl::VulkanInfo& info) const {
    return initialized_ ? api_.slSetVulkanInfo(info) : sl::Result::eErrorNotInitialized;
}
sl::Result StreamlineRuntime::supports(VkPhysicalDevice physical_device) const {
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical_device;
    return initialized_ ? api_.slIsFeatureSupported(sl::kFeatureDLSS, adapter)
                        : sl::Result::eErrorNotInitialized;
}
sl::Result StreamlineRuntime::shutdown() noexcept {
    if (!initialized_) return sl::Result::eOk;
    const auto result = api_.slShutdown();
    if (result == sl::Result::eOk) initialized_ = false;
    if (result != sl::Result::eOk)
        std::fprintf(stderr, "[Basix] slShutdown failed: result=%u\n", static_cast<unsigned>(result));
    return result;
}
} // namespace basix
