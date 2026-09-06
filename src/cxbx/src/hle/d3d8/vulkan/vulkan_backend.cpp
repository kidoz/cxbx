#include "vulkan_backend.h"

// The vendored DirectX SDK basetsd.h shadows the Windows SDK one and does
// not define POINTER_64, which winnt.h requires (same workaround as the
// other windows.h consumers in this tree).
#define POINTER_64 __ptr64

#include <windows.h>

#include <cstdio>
#include <cstring>

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <volk.h>

namespace cxbx
{
namespace d3d8
{
namespace vulkan
{

namespace
{
constexpr const char* kRequiredInstanceExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    (void)types;
    (void)userData;
    printf("VULKAN| validation [%d] %s\n", static_cast<int>(severity),
           data->pMessage != nullptr ? data->pMessage : "");
    fflush(stdout);
    return VK_FALSE;
}
} // namespace

bool SmokeBootstrap(const void* nativeWindow, bool validationLayers)
{
    if(nativeWindow == nullptr)
    {
        printf("VULKAN| no render window; cannot open a surface\n");
        return false;
    }
    if(volkInitialize() != VK_SUCCESS)
    {
        printf("VULKAN| Vulkan loader unavailable (vulkan-1.dll)\n");
        return false;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "cxbx";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const char* layerNames[1] = {};
    unsigned int layerCount = 0;
    const char* extensionNames[3] = {};
    unsigned int extensionCount = 0;
    for(const char* name : kRequiredInstanceExtensions)
    {
        extensionNames[extensionCount++] = name;
    }

    if(validationLayers)
    {
        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        unsigned int available = 0;
        vkEnumerateInstanceLayerProperties(&available, nullptr);
        VkLayerProperties* properties = new VkLayerProperties[available];
        vkEnumerateInstanceLayerProperties(&available, properties);
        bool found = false;
        for(unsigned int i = 0; i < available; ++i)
        {
            if(strcmp(properties[i].layerName, kValidationLayer) == 0)
            {
                found = true;
                break;
            }
        }
        delete[] properties;
        if(found)
        {
            layerNames[layerCount++] = kValidationLayer;
            extensionNames[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
        else
        {
            printf("VULKAN| CXBX_VULKAN_VALIDATE set but %s is not installed\n",
                   kValidationLayer);
        }
    }

    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = layerCount;
    instanceInfo.ppEnabledLayerNames = layerNames;
    instanceInfo.enabledExtensionCount = extensionCount;
    instanceInfo.ppEnabledExtensionNames = extensionNames;

    VkInstance instance = VK_NULL_HANDLE;
    if(vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
    {
        printf("VULKAN| vkCreateInstance failed\n");
        volkFinalize();
        return false;
    }
    volkLoadInstance(instance);

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if(validationLayers && layerCount != 0)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
        messengerInfo.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = DebugUtilsCallback;
        const auto vkCreateDebugUtilsMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if(vkCreateDebugUtilsMessenger != nullptr &&
           vkCreateDebugUtilsMessenger(instance, &messengerInfo, nullptr,
                                       &messenger) != VK_SUCCESS)
        {
            messenger = VK_NULL_HANDLE;
        }
    }

    // Pick the first Vulkan 1.3 physical device; discrete devices win when
    // several qualify. Migration phases that allocate GPU memory should
    // revisit the policy with a real budget (32-bit host address space).
    unsigned int deviceCount = 0;
    if(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS ||
       deviceCount == 0)
    {
        printf("VULKAN| no Vulkan physical devices found\n");
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }
    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties deviceProperties = {};
    for(unsigned int i = 0; i < deviceCount; ++i)
    {
        VkPhysicalDeviceProperties candidate = {};
        vkGetPhysicalDeviceProperties(devices[i], &candidate);
        printf("VULKAN| adapter: %s api=%u.%u.%u vendor=0x%X device=0x%X%s\n",
               candidate.deviceName,
               VK_API_VERSION_MAJOR(candidate.apiVersion),
               VK_API_VERSION_MINOR(candidate.apiVersion),
               VK_API_VERSION_PATCH(candidate.apiVersion),
               candidate.vendorID, candidate.deviceID,
               candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                   ? " (discrete)"
                   : "");
        if(candidate.apiVersion < VK_API_VERSION_1_3)
        {
            continue;
        }
        if(physicalDevice == VK_NULL_HANDLE ||
           (candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
        {
            physicalDevice = devices[i];
            deviceProperties = candidate;
        }
    }
    delete[] devices;
    if(physicalDevice == VK_NULL_HANDLE)
    {
        printf("VULKAN| no adapter with Vulkan >= 1.3\n");
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }

    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(const_cast<void*>(nativeWindow));
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if(vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface) !=
       VK_SUCCESS)
    {
        printf("VULKAN| vkCreateWin32SurfaceKHR failed\n");
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }

    unsigned int queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    VkQueueFamilyProperties* queueFamilies =
        new VkQueueFamilyProperties[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies);
    unsigned int queueFamily = queueFamilyCount;
    for(unsigned int i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface,
                                             &presentSupported);
        if(presentSupported &&
           (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            queueFamily = i;
            break;
        }
    }
    delete[] queueFamilies;
    if(queueFamily == queueFamilyCount)
    {
        printf("VULKAN| no queue family with graphics + present support\n");
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }

    unsigned int formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                         nullptr);
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[formatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                         formats);
    const VkSurfaceFormatKHR surfaceFormat = formats[0];
    delete[] formats;

    unsigned int presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                              &presentModeCount, nullptr);

    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                              &capabilities);

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    VkDevice device = VK_NULL_HANDLE;
    if(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) !=
       VK_SUCCESS)
    {
        printf("VULKAN| vkCreateDevice failed\n");
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }
    volkLoadDevice(device);

    // FIFO is the one present mode every implementation must support.
    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount =
        capabilities.minImageCount < 2
            ? 2
            : capabilities.minImageCount;
    if(capabilities.maxImageCount != 0 &&
       swapchainInfo.minImageCount > capabilities.maxImageCount)
    {
        swapchainInfo.minImageCount = capabilities.maxImageCount;
    }
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = capabilities.currentExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) !=
       VK_SUCCESS)
    {
        printf("VULKAN| vkCreateSwapchainKHR failed\n");
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }

    unsigned int imageCount = 0;
    if(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) !=
           VK_SUCCESS ||
       imageCount == 0)
    {
        printf("VULKAN| vkGetSwapchainImagesKHR returned no images\n");
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        return false;
    }

    printf("VULKAN| device ready: %u swapchain image(s), format=%d, extent=%ux%u, "
           "present modes=%u, queue family=%u\n",
           imageCount, static_cast<int>(surfaceFormat.format),
           capabilities.currentExtent.width, capabilities.currentExtent.height,
           presentModeCount, queueFamily);

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    if(messenger != VK_NULL_HANDLE)
    {
        const auto vkDestroyDebugUtilsMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance,
                                      "vkDestroyDebugUtilsMessengerEXT"));
        if(vkDestroyDebugUtilsMessenger != nullptr)
        {
            vkDestroyDebugUtilsMessenger(instance, messenger, nullptr);
        }
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    volkFinalize();
    return true;
}

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx
