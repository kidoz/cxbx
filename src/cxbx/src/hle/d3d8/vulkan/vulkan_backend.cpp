#include "vulkan_backend.h"

#include "vulkan_renderer.h"

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

// The presenter keeps the whole Vulkan object graph alive between frames.
// Calls are externally synchronized by the caller: Initialize happens before
// any present-path thread runs, and present-path frames arrive serialized.
struct PresenterState
{
    bool valid = false;
    bool presenting = false; // logged once on the first successful frame
    bool extentWarned = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    unsigned int queueFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    VkImage* swapchainImages = nullptr;
    unsigned int swapchainImageCount = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMapped = nullptr;
    VkDeviceSize stagingSize = 0;
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderSemaphore = VK_NULL_HANDLE;
};

PresenterState g_Presenter;

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
{
    (void)types;
    (void)userData;
    printf("VULKAN| validation [%d] %s\n", static_cast<int>(severity),
           data->pMessage != nullptr ? data->pMessage : "");
    fflush(stdout);
    return VK_FALSE;
}

unsigned int FindMemoryType(unsigned int typeBits,
                            VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(g_Presenter.physicalDevice,
                                        &memoryProperties);
    for(unsigned int i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool allowed = (typeBits & (1u << i)) != 0;
        const bool hasProperties =
            (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
            properties;
        if(allowed && hasProperties)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

// Releases the per-frame object graph (swapchain, command objects, staging,
// semaphores). Called between swapchain recreations and at shutdown.
void DestroyFrameObjects()
{
    if(g_Presenter.device == VK_NULL_HANDLE)
    {
        g_Presenter.swapchain = VK_NULL_HANDLE;
        g_Presenter.swapchainImageCount = 0;
        g_Presenter.swapchainImages = nullptr;
        return;
    }
    vkDeviceWaitIdle(g_Presenter.device);
    if(g_Presenter.acquireSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(g_Presenter.device, g_Presenter.acquireSemaphore,
                           nullptr);
        g_Presenter.acquireSemaphore = VK_NULL_HANDLE;
    }
    if(g_Presenter.renderSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(g_Presenter.device, g_Presenter.renderSemaphore,
                           nullptr);
        g_Presenter.renderSemaphore = VK_NULL_HANDLE;
    }
    if(g_Presenter.stagingMapped != nullptr)
    {
        vkUnmapMemory(g_Presenter.device, g_Presenter.stagingMemory);
        g_Presenter.stagingMapped = nullptr;
    }
    if(g_Presenter.stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_Presenter.device, g_Presenter.stagingBuffer, nullptr);
        g_Presenter.stagingBuffer = VK_NULL_HANDLE;
    }
    if(g_Presenter.stagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_Presenter.device, g_Presenter.stagingMemory, nullptr);
        g_Presenter.stagingMemory = VK_NULL_HANDLE;
    }
    g_Presenter.stagingSize = 0;
    if(g_Presenter.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(g_Presenter.device, g_Presenter.commandPool,
                             nullptr);
        g_Presenter.commandPool = VK_NULL_HANDLE;
        g_Presenter.commandBuffer = VK_NULL_HANDLE;
    }
    if(g_Presenter.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(g_Presenter.device, g_Presenter.swapchain,
                              nullptr);
        g_Presenter.swapchain = VK_NULL_HANDLE;
    }
    delete[] g_Presenter.swapchainImages;
    g_Presenter.swapchainImages = nullptr;
    g_Presenter.swapchainImageCount = 0;
    g_Presenter.stagingMapped = nullptr;
}

bool CreateFrameObjects()
{
    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_Presenter.physicalDevice,
                                              g_Presenter.surface,
                                              &capabilities);

    unsigned int formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_Presenter.physicalDevice,
                                         g_Presenter.surface, &formatCount,
                                         nullptr);
    if(formatCount == 0)
    {
        printf("VULKAN| surface reports no formats\n");
        return false;
    }
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[formatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_Presenter.physicalDevice,
                                         g_Presenter.surface, &formatCount,
                                         formats);
    const VkSurfaceFormatKHR surfaceFormat = formats[0];
    delete[] formats;

    unsigned int presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_Presenter.physicalDevice,
                                              g_Presenter.surface,
                                              &presentModeCount, nullptr);

    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = g_Presenter.surface;
    swapchainInfo.minImageCount =
        capabilities.minImageCount < 2 ? 2 : capabilities.minImageCount;
    if(capabilities.maxImageCount != 0 &&
       swapchainInfo.minImageCount > capabilities.maxImageCount)
    {
        swapchainInfo.minImageCount = capabilities.maxImageCount;
    }
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = capabilities.currentExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // FIFO is the one present mode every implementation must support; it
    // also matches the guest's 60 Hz present pacing.
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    if(vkCreateSwapchainKHR(g_Presenter.device, &swapchainInfo, nullptr,
                            &g_Presenter.swapchain) != VK_SUCCESS)
    {
        printf("VULKAN| vkCreateSwapchainKHR failed\n");
        return false;
    }
    g_Presenter.swapchainFormat = surfaceFormat.format;
    g_Presenter.extent = capabilities.currentExtent;

    unsigned int imageCount = 0;
    if(vkGetSwapchainImagesKHR(g_Presenter.device, g_Presenter.swapchain,
                               &imageCount, nullptr) != VK_SUCCESS ||
       imageCount == 0)
    {
        printf("VULKAN| vkGetSwapchainImagesKHR returned no images\n");
        return false;
    }
    g_Presenter.swapchainImages = new VkImage[imageCount];
    vkGetSwapchainImagesKHR(g_Presenter.device, g_Presenter.swapchain,
                            &imageCount, g_Presenter.swapchainImages);
    g_Presenter.swapchainImageCount = imageCount;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = g_Presenter.queueFamily;
    if(vkCreateCommandPool(g_Presenter.device, &poolInfo, nullptr,
                           &g_Presenter.commandPool) != VK_SUCCESS)
    {
        printf("VULKAN| command pool creation failed\n");
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo = {};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = g_Presenter.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if(vkAllocateCommandBuffers(g_Presenter.device, &commandInfo,
                                &g_Presenter.commandBuffer) != VK_SUCCESS)
    {
        printf("VULKAN| command buffer allocation failed\n");
        return false;
    }

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if(vkCreateSemaphore(g_Presenter.device, &semaphoreInfo, nullptr,
                         &g_Presenter.acquireSemaphore) != VK_SUCCESS ||
       vkCreateSemaphore(g_Presenter.device, &semaphoreInfo, nullptr,
                         &g_Presenter.renderSemaphore) != VK_SUCCESS)
    {
        printf("VULKAN| semaphore creation failed\n");
        return false;
    }

    printf("VULKAN| device ready: %u swapchain image(s), format=%d, "
           "extent=%ux%u, present modes=%u, queue family=%u\n",
           imageCount, static_cast<int>(surfaceFormat.format),
           capabilities.currentExtent.width,
           capabilities.currentExtent.height, presentModeCount,
           g_Presenter.queueFamily);
    return true;
}

// Allocates (or grows) the host-visible upload buffer used to hand frame
// pixels to the copy queue.
bool EnsureStagingCapacity(VkDeviceSize needed)
{
    if(g_Presenter.stagingSize >= needed)
    {
        return true;
    }
    if(g_Presenter.stagingMapped != nullptr)
    {
        vkUnmapMemory(g_Presenter.device, g_Presenter.stagingMemory);
        g_Presenter.stagingMapped = nullptr;
    }
    if(g_Presenter.stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_Presenter.device, g_Presenter.stagingBuffer, nullptr);
        g_Presenter.stagingBuffer = VK_NULL_HANDLE;
    }
    if(g_Presenter.stagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_Presenter.device, g_Presenter.stagingMemory, nullptr);
        g_Presenter.stagingMemory = VK_NULL_HANDLE;
    }
    g_Presenter.stagingSize = 0;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = needed;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateBuffer(g_Presenter.device, &bufferInfo, nullptr,
                      &g_Presenter.stagingBuffer) != VK_SUCCESS)
    {
        printf("VULKAN| staging buffer creation failed\n");
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(g_Presenter.device, g_Presenter.stagingBuffer,
                                  &requirements);
    const unsigned int memoryType = FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(memoryType == UINT32_MAX)
    {
        printf("VULKAN| no host-visible memory type for staging\n");
        return false;
    }
    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if(vkAllocateMemory(g_Presenter.device, &allocateInfo, nullptr,
                        &g_Presenter.stagingMemory) != VK_SUCCESS)
    {
        printf("VULKAN| staging memory allocation failed\n");
        return false;
    }
    if(vkBindBufferMemory(g_Presenter.device, g_Presenter.stagingBuffer,
                          g_Presenter.stagingMemory, 0) != VK_SUCCESS)
    {
        printf("VULKAN| staging buffer bind failed\n");
        return false;
    }
    if(vkMapMemory(g_Presenter.device, g_Presenter.stagingMemory, 0, needed, 0,
                   &g_Presenter.stagingMapped) != VK_SUCCESS)
    {
        printf("VULKAN| staging memory map failed\n");
        return false;
    }
    g_Presenter.stagingSize = needed;
    return true;
}

} // namespace

bool Initialize(const void* nativeWindow, bool validationLayers)
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

    if(vkCreateInstance(&instanceInfo, nullptr, &g_Presenter.instance) !=
       VK_SUCCESS)
    {
        printf("VULKAN| vkCreateInstance failed\n");
        volkFinalize();
        return false;
    }
    volkLoadInstance(g_Presenter.instance);

    if(validationLayers && layerCount != 0)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
        messengerInfo.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = DebugUtilsCallback;
        const auto vkCreateDebugUtilsMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(g_Presenter.instance,
                                      "vkCreateDebugUtilsMessengerEXT"));
        if(vkCreateDebugUtilsMessenger != nullptr &&
           vkCreateDebugUtilsMessenger(g_Presenter.instance, &messengerInfo,
                                       nullptr,
                                       &g_Presenter.messenger) != VK_SUCCESS)
        {
            g_Presenter.messenger = VK_NULL_HANDLE;
        }
    }

    // Pick the first Vulkan 1.3 physical device; discrete devices win when
    // several qualify. Migration phases that allocate GPU memory should
    // revisit the policy with a real budget (32-bit host address space).
    unsigned int deviceCount = 0;
    if(vkEnumeratePhysicalDevices(g_Presenter.instance, &deviceCount,
                                  nullptr) != VK_SUCCESS ||
       deviceCount == 0)
    {
        printf("VULKAN| no Vulkan physical devices found\n");
        Shutdown();
        return false;
    }
    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    vkEnumeratePhysicalDevices(g_Presenter.instance, &deviceCount, devices);
    VkPhysicalDeviceProperties deviceProperties = {};
    for(unsigned int i = 0; i < deviceCount; ++i)
    {
        VkPhysicalDeviceProperties candidate = {};
        vkGetPhysicalDeviceProperties(devices[i], &candidate);
        printf("VULKAN| adapter: %s api=%u.%u.%u vendor=0x%X device=0x%X%s\n",
               candidate.deviceName,
               VK_API_VERSION_MAJOR(candidate.apiVersion),
               VK_API_VERSION_MINOR(candidate.apiVersion),
               VK_API_VERSION_PATCH(candidate.apiVersion), candidate.vendorID,
               candidate.deviceID,
               candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                   ? " (discrete)"
                   : "");
        if(candidate.apiVersion < VK_API_VERSION_1_3)
        {
            continue;
        }
        if(g_Presenter.physicalDevice == VK_NULL_HANDLE ||
           (candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            deviceProperties.deviceType !=
                VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
        {
            g_Presenter.physicalDevice = devices[i];
            deviceProperties = candidate;
        }
    }
    delete[] devices;
    if(g_Presenter.physicalDevice == VK_NULL_HANDLE)
    {
        printf("VULKAN| no adapter with Vulkan >= 1.3\n");
        Shutdown();
        return false;
    }

    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(const_cast<void*>(nativeWindow));
    if(vkCreateWin32SurfaceKHR(g_Presenter.instance, &surfaceInfo, nullptr,
                               &g_Presenter.surface) != VK_SUCCESS)
    {
        printf("VULKAN| vkCreateWin32SurfaceKHR failed\n");
        Shutdown();
        return false;
    }

    unsigned int queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_Presenter.physicalDevice,
                                             &queueFamilyCount, nullptr);
    VkQueueFamilyProperties* queueFamilies =
        new VkQueueFamilyProperties[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(g_Presenter.physicalDevice,
                                             &queueFamilyCount, queueFamilies);
    unsigned int queueFamily = queueFamilyCount;
    for(unsigned int i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_Presenter.physicalDevice, i,
                                             g_Presenter.surface,
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
        Shutdown();
        return false;
    }
    g_Presenter.queueFamily = queueFamily;

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    // The fixed-function render path records into dynamic rendering
    // instances (P2), which is a Vulkan 1.3 feature that must be enabled.
    VkPhysicalDeviceVulkan13Features vulkan13Features = {};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &vulkan13Features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    if(vkCreateDevice(g_Presenter.physicalDevice, &deviceInfo, nullptr,
                      &g_Presenter.device) != VK_SUCCESS)
    {
        printf("VULKAN| vkCreateDevice failed\n");
        Shutdown();
        return false;
    }
    volkLoadDevice(g_Presenter.device);
    vkGetDeviceQueue(g_Presenter.device, queueFamily, 0, &g_Presenter.queue);

    // Bring the render target up with the presenter's surface extent; the
    // HLE corrects it to the device backbuffer size via SetTargetSize
    // before the first draw.
    if(!RendererInitialize(g_Presenter.device, g_Presenter.physicalDevice,
                           g_Presenter.queue, queueFamily,
                           640, 480))
    {
        printf("VULKAN| renderer init failed; present-only mode\n");
    }

    if(!CreateFrameObjects())
    {
        Shutdown();
        return false;
    }

    g_Presenter.valid = true;
    printf("VULKAN| presenter ready; CXBX_HOST_BACKEND=vulkan owns the "
           "window surface\n");
    return true;
}

bool PresentFrame(const void* pixels, unsigned int width, unsigned int height,
                  unsigned int pitch)
{
    if(!g_Presenter.valid)
    {
        return false;
    }

    // P2 render path: the renderer owns the frame content; the caller's
    // pixels (a d3d8 backbuffer read) are ignored and the render target is
    // the present source.
    const bool renderPath = RendererValid();
    if(!renderPath && (pixels == nullptr || width == 0 || height == 0 ||
                       pitch == 0))
    {
        return false;
    }

    unsigned int copyWidth = 0;
    unsigned int copyHeight = 0;
    if(!renderPath)
    {
        // Staging rows are the source pitch; the copy region is the overlap
        // of the frame and the swapchain extent. They match whenever the
        // window was not resized; a 1:1 top-left copy is the P1 behavior
        // otherwise.
        copyWidth =
            width < g_Presenter.extent.width ? width : g_Presenter.extent.width;
        copyHeight = height < g_Presenter.extent.height
                         ? height
                         : g_Presenter.extent.height;
        if((copyWidth < width || copyHeight < height) &&
           !g_Presenter.extentWarned)
        {
            g_Presenter.extentWarned = true;
            printf("VULKAN| frame %ux%u exceeds surface %ux%u; presenting the "
                   "top-left region (resize recreation lands with P2)\n",
                   width, height, g_Presenter.extent.width,
                   g_Presenter.extent.height);
        }

        const VkDeviceSize needed = static_cast<VkDeviceSize>(pitch) * height;
        if(!EnsureStagingCapacity(needed))
        {
            g_Presenter.valid = false;
            return false;
        }
        memcpy(g_Presenter.stagingMapped, pixels, static_cast<size_t>(needed));
    }

    // One bounded recreation attempt: an out-of-date swapchain is rebuilt
    // and the frame is retried; anything else hands presentation back to
    // d3d8. Failures after a successful acquire latch the presenter off, so
    // a consumed semaphore can never be signaled twice.
    bool recreated = false;
    unsigned int imageIndex = 0;
    VkResult acquired = VK_INCOMPLETE;
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        acquired = vkAcquireNextImageKHR(g_Presenter.device,
                                         g_Presenter.swapchain, UINT64_MAX,
                                         g_Presenter.acquireSemaphore,
                                         VK_NULL_HANDLE, &imageIndex);
        if(acquired == VK_ERROR_OUT_OF_DATE_KHR && !recreated)
        {
            recreated = true;
            DestroyFrameObjects();
            if(!CreateFrameObjects())
            {
                g_Presenter.valid = false;
                return false;
            }
            continue;
        }
        break;
    }
    if(acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
    {
        printf("VULKAN| vkAcquireNextImageKHR failed (%d)\n",
               static_cast<int>(acquired));
        return false;
    }

    if(renderPath)
    {
        // Submit the pending render batch, copy the target into the
        // acquired swapchain image (renderer transitions it to present
        // source), then hand the image to the presentation engine.
        if(!RendererCopyToSwapchain(g_Presenter.swapchainImages[imageIndex],
                                    g_Presenter.extent.width,
                                    g_Presenter.extent.height))
        {
            g_Presenter.valid = false;
            return false;
        }

        VkPresentInfoKHR renderPresent = {};
        renderPresent.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        renderPresent.swapchainCount = 1;
        renderPresent.pSwapchains = &g_Presenter.swapchain;
        renderPresent.pImageIndices = &imageIndex;
        const VkResult presentedRender =
            vkQueuePresentKHR(g_Presenter.queue, &renderPresent);
        if(presentedRender == VK_ERROR_OUT_OF_DATE_KHR)
        {
            DestroyFrameObjects();
            if(!CreateFrameObjects())
            {
                g_Presenter.valid = false;
                return false;
            }
            return true;
        }
        if(presentedRender != VK_SUCCESS && presentedRender != VK_SUBOPTIMAL_KHR)
        {
            printf("VULKAN| vkQueuePresentKHR failed (%d)\n",
                   static_cast<int>(presentedRender));
            g_Presenter.valid = false;
            return false;
        }
        if(!g_Presenter.presenting)
        {
            g_Presenter.presenting = true;
            printf("VULKAN| presenting the render target via swapchain\n");
        }
        return true;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_Presenter.commandBuffer, 0);
    if(vkBeginCommandBuffer(g_Presenter.commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        printf("VULKAN| vkBeginCommandBuffer failed\n");
        g_Presenter.valid = false;
        return false;
    }

    // The acquired image's layout is discardable, so the source layout is
    // recorded as UNDEFINED (discard) whatever the presentation engine left.
    VkImageMemoryBarrier toTransfer = {};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = g_Presenter.swapchainImages[imageIndex];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_Presenter.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransfer);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = pitch / 4; // BGRA texel row length
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { copyWidth, copyHeight, 1 };
    vkCmdCopyBufferToImage(g_Presenter.commandBuffer, g_Presenter.stagingBuffer,
                           g_Presenter.swapchainImages[imageIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toPresent = toTransfer;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(g_Presenter.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toPresent);
    if(vkEndCommandBuffer(g_Presenter.commandBuffer) != VK_SUCCESS)
    {
        printf("VULKAN| vkEndCommandBuffer failed\n");
        g_Presenter.valid = false;
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &g_Presenter.acquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_Presenter.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &g_Presenter.renderSemaphore;
    if(vkQueueSubmit(g_Presenter.queue, 1, &submitInfo, VK_NULL_HANDLE) !=
       VK_SUCCESS)
    {
        printf("VULKAN| vkQueueSubmit failed\n");
        g_Presenter.valid = false;
        return false;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_Presenter.renderSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_Presenter.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(g_Presenter.queue, &presentInfo);
    if(presented == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // The submitted frame lands or is discarded harmlessly; recreate so
        // the next frame has a current swapchain.
        DestroyFrameObjects();
        if(!CreateFrameObjects())
        {
            g_Presenter.valid = false;
            return false;
        }
        return true;
    }
    if(presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
    {
        printf("VULKAN| vkQueuePresentKHR failed (%d)\n",
               static_cast<int>(presented));
        g_Presenter.valid = false;
        return false;
    }

    // P1 keeps the frame graph trivially synchronous: one upload+copy per
    // present, fully drained before the next frame is recorded. The queue
    // work per frame is a single host-side copy, so this costs nothing next
    // to the d3d8 backbuffer readback that feeds it.
    vkQueueWaitIdle(g_Presenter.queue);

    if(!g_Presenter.presenting)
    {
        g_Presenter.presenting = true;
        printf("VULKAN| presenting via swapchain (frame upload %ux%u)\n", width,
               height);
    }
    return true;
}

bool PresenterValid()
{
    return g_Presenter.valid;
}

void SetTargetSize(unsigned int width, unsigned int height)
{
    if(!g_Presenter.valid || width == 0 || height == 0)
    {
        return;
    }
    if(!RendererValid())
    {
        if(!RendererInitialize(g_Presenter.device, g_Presenter.physicalDevice,
                               g_Presenter.queue, g_Presenter.queueFamily,
                               width, height))
        {
            printf("VULKAN| renderer init failed at target size %ux%u; "
                   "present-only mode\n",
                   width, height);
        }
        return;
    }
    // A live renderer with different dimensions is a mode change P2 does not
    // handle mid-session; titles set the size before the first draw, so this
    // path is diagnostic-only in practice.
    printf("VULKAN| SetTargetSize(%ux%u) ignored: renderer already running\n",
           width, height);
}

void Shutdown()
{
    if(g_Presenter.instance == VK_NULL_HANDLE)
    {
        return;
    }
    DestroyFrameObjects();
    RendererShutdown();
    if(g_Presenter.device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(g_Presenter.device, nullptr);
        g_Presenter.device = VK_NULL_HANDLE;
    }
    if(g_Presenter.messenger != VK_NULL_HANDLE)
    {
        const auto vkDestroyDebugUtilsMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(g_Presenter.instance,
                                      "vkDestroyDebugUtilsMessengerEXT"));
        if(vkDestroyDebugUtilsMessenger != nullptr)
        {
            vkDestroyDebugUtilsMessenger(g_Presenter.instance,
                                         g_Presenter.messenger, nullptr);
        }
        g_Presenter.messenger = VK_NULL_HANDLE;
    }
    if(g_Presenter.surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(g_Presenter.instance, g_Presenter.surface, nullptr);
        g_Presenter.surface = VK_NULL_HANDLE;
    }
    if(g_Presenter.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(g_Presenter.instance, nullptr);
        g_Presenter.instance = VK_NULL_HANDLE;
    }
    g_Presenter.physicalDevice = VK_NULL_HANDLE;
    g_Presenter.queue = VK_NULL_HANDLE;
    g_Presenter.valid = false;
    g_Presenter.presenting = false;
    g_Presenter.extentWarned = false;
    volkFinalize();
}

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx
