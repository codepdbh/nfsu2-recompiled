// Custom Vulkan driver loading through libadrenotools, as in the MW and Carbon ports.
#include "vulkan_driver.h"
#ifdef NFS_CUSTOM_DRIVERS
#include <adrenotools/driver.h>
#endif
#include <android/log.h>
#include <dlfcn.h>
#include <string>

void* nfsOpenVulkan(const char* hooks, const char* temp, const char* directory, const char* library) {
    if (!library || !*library) return dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#ifndef NFS_CUSTOM_DRIVERS
    // 32-bit builds: libadrenotools is arm64-only, so only the system driver exists.
    (void)hooks; (void)temp; (void)directory;
    __android_log_print(ANDROID_LOG_ERROR, "NFSU2", "Custom Vulkan drivers need the 64-bit build: %s", library);
    return nullptr;
#else
    if (!hooks || !*hooks || !directory || !*directory) return nullptr;
    std::string path(directory);
    if (path.back() != '/') path += '/';
    void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM,
        temp && *temp ? temp : nullptr, hooks, path.c_str(), library, nullptr, nullptr);
    __android_log_print(handle ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "NFSU2",
        "Custom Vulkan driver %s: %s%s", handle ? "opened" : "failed to open", path.c_str(), library);
    return handle;
#endif
}

PFN_vkGetInstanceProcAddr nfsVulkanEntry(void* handle) {
    return handle ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr")) : nullptr;
}
