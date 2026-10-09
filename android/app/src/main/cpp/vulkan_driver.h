#pragma once
#include <vulkan/vulkan.h>

// Opens the Vulkan driver to use: the system libvulkan, or an imported adrenotools package
// (Turnip, ...) when `library` is set. Returns the dlopen handle; null means the selected
// driver could not be opened (never a silent fallback to the system one).
void* nfsOpenVulkan(const char* hooks, const char* temp, const char* directory, const char* library);
PFN_vkGetInstanceProcAddr nfsVulkanEntry(void* handle);
