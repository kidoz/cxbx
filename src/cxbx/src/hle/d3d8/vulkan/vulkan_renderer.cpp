#include "vulkan_renderer.h"

// The renderer serializes its public entries with a critical section; the
// POINTER_64 define matches the other windows.h consumers in this tree (the
// vendored DirectX SDK basetsd.h shadows the Windows SDK one).
#define POINTER_64 __ptr64

#include <windows.h>

#include "shader_spirv.h"

#include <cstdio>
#include <cstring>
#include <map>

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
// Host D3DPRIMITIVETYPE values the P2 path understands.
constexpr unsigned int kPointList = 1;
constexpr unsigned int kLineList = 2;
constexpr unsigned int kLineStrip = 3;
constexpr unsigned int kTriangleList = 4;
constexpr unsigned int kTriangleStrip = 5;
constexpr unsigned int kTriangleFan = 6;
constexpr unsigned int kNoDiffuse = 0xFFFFFFFFu;
constexpr unsigned int kNoTexCoord = 0xFFFFFFFFu;
constexpr unsigned int kVertexStagingInitial = 256 * 1024;
constexpr unsigned int kMaxTextures = 48;
constexpr unsigned int kMaxDescriptorSets = 512;

// d3d8 texture-stage defaults (stage 0 MODULATEs, later stages disabled).
constexpr unsigned int kDefaultColorOp = 4;    // D3DTOP_MODULATE
constexpr unsigned int kDefaultColorOpOff = 1; // D3DTOP_DISABLE
constexpr unsigned int kDefaultArg1 = 2;       // D3DTA_TEXTURE
constexpr unsigned int kDefaultArg2 = 1;       // D3DTA_CURRENT

struct RendererTexture
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int hostFormat = 0;
    void* key = nullptr;
    unsigned long long lastUse = 0;
};

struct RendererState
{
    bool valid = false;
    bool frameOpen = false;
    bool renderingActive = false;
    bool unsupportedLogged = false;
    bool textureFormatLogged = false;
    unsigned int width = 640;
    unsigned int height = 480;
    float viewport[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
    float pendingClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool hasPendingClear = false;

    // Texture-stage state (d3d8 values), stage order 0..3.
    std::uint32_t stageOp[4] = { kDefaultColorOp, kDefaultColorOpOff,
                                 kDefaultColorOpOff, kDefaultColorOpOff };
    std::uint32_t stageArg1[4] = { kDefaultArg1, kDefaultArg1, kDefaultArg1,
                                   kDefaultArg1 };
    std::uint32_t stageArg2[4] = { kDefaultArg2, kDefaultArg2, kDefaultArg2,
                                   kDefaultArg2 };
    std::uint32_t stageAddressU[4] = {}; // d3d8 D3DTADDRESS_WRAP = 1
    std::uint32_t stageAddressV[4] = {};
    std::uint32_t stageMagFilter[4] = {}; // d3d8 D3DTEXF_POINT = 1, LINEAR = 2
    std::uint32_t stageMinFilter[4] = {};
    RendererTexture* stageTexture[4] = {};

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    unsigned int queueFamily = 0;
    unsigned int memoryTypes = 0;

    VkImage target = VK_NULL_HANDLE;
    VkDeviceMemory targetMemory = VK_NULL_HANDLE;
    VkImageView targetView = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkBuffer vertexStaging = VK_NULL_HANDLE;
    VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
    void* vertexMapped = nullptr;
    VkDeviceSize vertexStagingSize = 0;
    VkDeviceSize vertexCursor = 0;

    VkBuffer readbackStaging = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    void* readbackMapped = nullptr;

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::map<unsigned long long, VkPipeline> pipelines;

    // Texture mirror (P3): guest textures pulled at bind time.
    RendererTexture textures[kMaxTextures] = {};
    unsigned int textureCount = 0;
    unsigned long long textureUseCounter = 0;
    VkBuffer textureStaging = VK_NULL_HANDLE;
    VkDeviceMemory textureStagingMemory = VK_NULL_HANDLE;
    void* textureMapped = nullptr;
    VkDeviceSize textureStagingSize = 0;
    VkImageView dummyView = VK_NULL_HANDLE;
    VkImage dummyImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
    std::map<unsigned long long, VkSampler> samplers;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

RendererState g_R;

// The renderer is entered from the guest execution thread, the device proxy
// thread (initial present), and present/readback paths; all public entries
// serialize here because they share one command buffer.
CRITICAL_SECTION g_RendererLock;

struct RendererLockScope
{
    RendererLockScope() { EnterCriticalSection(&g_RendererLock); }
    ~RendererLockScope() { LeaveCriticalSection(&g_RendererLock); }
};

unsigned int FindMemoryType(unsigned int typeBits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props = {};
    vkGetPhysicalDeviceMemoryProperties(g_R.physicalDevice, &props);
    for(unsigned int i = 0; i < props.memoryTypeCount; ++i)
    {
        if((typeBits & (1u << i)) != 0 &&
           (props.memoryTypes[i].propertyFlags & flags) == flags)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

bool AllocateDeviceMemory(VkDeviceMemory* memory, VkDeviceSize size,
                          unsigned int typeBits,
                          VkMemoryPropertyFlags flags)
{
    VkMemoryAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = size;
    info.memoryTypeIndex = FindMemoryType(typeBits, flags);
    if(info.memoryTypeIndex == UINT32_MAX)
    {
        return false;
    }
    return vkAllocateMemory(g_R.device, &info, nullptr, memory) == VK_SUCCESS;
}

bool CreatePipeline(unsigned long long key, VkPrimitiveTopology topology,
                    unsigned int stride, bool hasDiffuse,
                    unsigned int diffuseOffset, unsigned int texCoordOffset)
{
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3] = {};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset = 0;
    unsigned int attributeCount = 1;
    if(hasDiffuse)
    {
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[1].offset = diffuseOffset;
        attributeCount = 2;
    }
    // The vertex shader always reads the texcoord; draws without one bind
    // offset 0, whose values are only consumed when a stage enables a
    // TEXTURE argument (those draws always supply real texcoords).
    attributes[attributeCount].location = 2;
    attributes[attributeCount].binding = 0;
    attributes[attributeCount].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[attributeCount].offset =
        texCoordOffset != kNoTexCoord ? texCoordOffset : 0;
    ++attributeCount;

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // D3D8 defaults: blending disabled, alpha test off.
    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g_R.vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g_R.fragmentShader;
    stages[1].pName = "main";

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = g_R.pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // dynamic rendering

    VkPipeline pipeline = VK_NULL_HANDLE;
    if(vkCreateGraphicsPipelines(g_R.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                 nullptr, &pipeline) != VK_SUCCESS)
    {
        printf("VULKAN| pipeline creation failed (topology=%u stride=%u)\n",
               static_cast<unsigned>(topology), stride);
        return false;
    }
    g_R.pipelines[key] = pipeline;
    return true;
}

VkPipeline PipelineFor(unsigned int primitiveType, unsigned int stride,
                       unsigned int diffuseOffset, unsigned int texCoordOffset)
{
    VkPrimitiveTopology topology;
    switch(primitiveType)
    {
        case kPointList:
            topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case kLineList:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case kLineStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            break;
        case kTriangleStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case kTriangleFan:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            break;
        default:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
    }
    const unsigned long long key =
        static_cast<unsigned long long>(topology) |
        (static_cast<unsigned long long>(stride) << 8) |
        (static_cast<unsigned long long>(diffuseOffset != kNoDiffuse ? 1u : 0u)
         << 40) |
        (static_cast<unsigned long long>(diffuseOffset) << 44) |
        (static_cast<unsigned long long>(texCoordOffset) << 20);
    auto found = g_R.pipelines.find(key);
    if(found != g_R.pipelines.end())
    {
        return found->second;
    }
    return CreatePipeline(key, topology, stride, diffuseOffset != kNoDiffuse,
                          diffuseOffset, texCoordOffset)
               ? g_R.pipelines[key]
               : VK_NULL_HANDLE;
}

void CloseRendering()
{
    if(g_R.renderingActive)
    {
        vkCmdEndRendering(g_R.commandBuffer);
        g_R.renderingActive = false;
    }
}

bool OpenFrame()
{
    if(g_R.frameOpen)
    {
        return true;
    }
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    if(vkBeginCommandBuffer(g_R.commandBuffer, &begin) != VK_SUCCESS)
    {
        printf("VULKAN| renderer command buffer begin failed\n");
        return false;
    }
    g_R.frameOpen = true;
    g_R.vertexCursor = 0;
    return true;
}

bool SubmitFrame()
{
    if(!g_R.frameOpen)
    {
        return true;
    }
    // A clear with no following draw never opens a rendering instance, so
    // its pending color is applied directly (the target rests in GENERAL).
    if(g_R.hasPendingClear && !g_R.renderingActive)
    {
        VkClearColorValue value = {};
        memcpy(value.float32, g_R.pendingClear, sizeof(g_R.pendingClear));
        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(g_R.commandBuffer, g_R.target,
                             VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
        g_R.hasPendingClear = false;
    }
    CloseRendering();
    if(vkEndCommandBuffer(g_R.commandBuffer) != VK_SUCCESS)
    {
        printf("VULKAN| renderer command buffer end failed\n");
        g_R.frameOpen = false;
        return false;
    }
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_R.commandBuffer;
    if(vkQueueSubmit(g_R.queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        printf("VULKAN| renderer submit failed\n");
        g_R.frameOpen = false;
        return false;
    }
    vkQueueWaitIdle(g_R.queue);
    g_R.frameOpen = false;
    g_R.vertexCursor = 0;
    // Draws recorded this frame referenced descriptor sets from the pool;
    // nothing is in flight after the idle wait, so the pool recycles.
    vkResetDescriptorPool(g_R.device, g_R.descriptorPool, 0);
    g_R.descriptorSet = VK_NULL_HANDLE;
    return true;
}

// Host-format byte size for the supported mirror formats (0 = unsupported).
unsigned int TextureFormatBytesPerPixel(unsigned int hostFormat,
                                        VkFormat* formatOut)
{
    switch(hostFormat)
    {
        case 21: // D3DFMT_A8R8G8B8
        case 22: // D3DFMT_X8R8G8B8
            *formatOut = VK_FORMAT_B8G8R8A8_UNORM;
            return 4;
        case 23: // D3DFMT_R5G6B5
            *formatOut = VK_FORMAT_B5G6R5_UNORM_PACK16;
            return 2;
        case 25: // D3DFMT_A1R5G5B5
            *formatOut = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
            return 2;
        case 26: // D3DFMT_A4R4G4B4
            *formatOut = VK_FORMAT_B4G4R4A4_UNORM_PACK16;
            return 2;
        default:
            *formatOut = VK_FORMAT_UNDEFINED;
            return 0;
    }
}

bool EnsureTextureStaging(VkDeviceSize needed)
{
    if(g_R.textureStagingSize >= needed)
    {
        return true;
    }
    if(g_R.textureMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.textureStagingMemory);
        g_R.textureMapped = nullptr;
    }
    if(g_R.textureStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.textureStaging, nullptr);
        g_R.textureStaging = VK_NULL_HANDLE;
    }
    if(g_R.textureStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.textureStagingMemory, nullptr);
        g_R.textureStagingMemory = VK_NULL_HANDLE;
    }
    g_R.textureStagingSize = 0;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = needed;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(vkCreateBuffer(g_R.device, &bufferInfo, nullptr,
                      &g_R.textureStaging) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(g_R.device, g_R.textureStaging,
                                  &requirements);
    if(!AllocateDeviceMemory(&g_R.textureStagingMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
       vkBindBufferMemory(g_R.device, g_R.textureStaging,
                          g_R.textureStagingMemory, 0) != VK_SUCCESS ||
       vkMapMemory(g_R.device, g_R.textureStagingMemory, 0, needed, 0,
                   &g_R.textureMapped) != VK_SUCCESS)
    {
        return false;
    }
    g_R.textureStagingSize = needed;
    return true;
}

void DestroyTextureEntry(RendererTexture* texture)
{
    if(texture->view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(g_R.device, texture->view, nullptr);
    }
    if(texture->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(g_R.device, texture->image, nullptr);
    }
    if(texture->memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, texture->memory, nullptr);
    }
    *texture = {};
}

VkSampler SamplerFor(unsigned int stage)
{
    // d3d8 values normalize to their defaults when never set.
    const unsigned int addressU =
        g_R.stageAddressU[stage] != 0 ? g_R.stageAddressU[stage] : 1;
    const unsigned int addressV =
        g_R.stageAddressV[stage] != 0 ? g_R.stageAddressV[stage] : 1;
    const unsigned int magFilter =
        g_R.stageMagFilter[stage] != 0 ? g_R.stageMagFilter[stage] : 1;
    const unsigned int minFilter =
        g_R.stageMinFilter[stage] != 0 ? g_R.stageMinFilter[stage] : 1;

    const unsigned long long key =
        addressU | (addressV << 8) | (magFilter << 16) | (minFilter << 24);
    auto found = g_R.samplers.find(key);
    if(found != g_R.samplers.end())
    {
        return found->second;
    }

    const auto addressMode = [](unsigned int value)
    {
        switch(value)
        {
            case 2:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case 3:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case 4:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case 5:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    const auto filter = [](unsigned int value)
    {
        return value == 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    };

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter(magFilter);
    samplerInfo.minFilter = filter(minFilter);
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = addressMode(addressU);
    samplerInfo.addressModeV = addressMode(addressV);
    samplerInfo.addressModeW = addressMode(addressU);
    samplerInfo.maxLod = 0.0f; // level 0 only in P3
    samplerInfo.minLod = 0.0f;
    VkSampler sampler = VK_NULL_HANDLE;
    if(vkCreateSampler(g_R.device, &samplerInfo, nullptr, &sampler) !=
       VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    g_R.samplers[key] = sampler;
    return sampler;
}

bool EnsureStaging(VkDeviceSize needed)
{
    if(g_R.vertexStagingSize >= needed)
    {
        return true;
    }
    if(g_R.vertexMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.vertexStagingMemory);
        g_R.vertexMapped = nullptr;
    }
    if(g_R.vertexStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.vertexStaging, nullptr);
        g_R.vertexStaging = VK_NULL_HANDLE;
    }
    if(g_R.vertexStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.vertexStagingMemory, nullptr);
        g_R.vertexStagingMemory = VK_NULL_HANDLE;
    }
    g_R.vertexStagingSize = 0;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = needed;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(vkCreateBuffer(g_R.device, &bufferInfo, nullptr, &g_R.vertexStaging) !=
       VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(g_R.device, g_R.vertexStaging,
                                  &requirements);
    if(!AllocateDeviceMemory(&g_R.vertexStagingMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        return false;
    }
    if(vkBindBufferMemory(g_R.device, g_R.vertexStaging,
                          g_R.vertexStagingMemory, 0) != VK_SUCCESS)
    {
        return false;
    }
    if(vkMapMemory(g_R.device, g_R.vertexStagingMemory, 0, needed, 0,
                   &g_R.vertexMapped) != VK_SUCCESS)
    {
        return false;
    }
    g_R.vertexStagingSize = needed;
    return true;
}

} // namespace

bool RendererInitialize(void* deviceHandle, void* physicalDeviceHandle,
                        void* queueHandle, unsigned int queueFamily,
                        unsigned int width, unsigned int height)
{
    const VkDevice device = static_cast<VkDevice>(deviceHandle);
    const VkPhysicalDevice physicalDevice =
        static_cast<VkPhysicalDevice>(physicalDeviceHandle);
    const VkQueue queue = static_cast<VkQueue>(queueHandle);
    InitializeCriticalSectionAndSpinCount(&g_RendererLock, 0x400);
    g_R.device = device;
    g_R.physicalDevice = physicalDevice;
    g_R.queue = queue;
    g_R.queueFamily = queueFamily;
    g_R.width = width != 0 ? width : 640;
    g_R.height = height != 0 ? height : 480;
    g_R.viewport[0] = 0.0f;
    g_R.viewport[1] = 0.0f;
    g_R.viewport[2] = static_cast<float>(g_R.width);
    g_R.viewport[3] = static_cast<float>(g_R.height);

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent = { g_R.width, g_R.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(device, &imageInfo, nullptr, &g_R.target) != VK_SUCCESS)
    {
        printf("VULKAN| renderer target creation failed\n");
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(device, g_R.target, &requirements);
    g_R.memoryTypes = requirements.memoryTypeBits;
    if(!AllocateDeviceMemory(&g_R.targetMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    {
        printf("VULKAN| renderer target memory failed\n");
        RendererShutdown();
        return false;
    }
    if(vkBindImageMemory(device, g_R.target, g_R.targetMemory, 0) != VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = g_R.target;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if(vkCreateImageView(device, &viewInfo, nullptr, &g_R.targetView) !=
       VK_SUCCESS)
    {
        printf("VULKAN| renderer target view failed\n");
        RendererShutdown();
        return false;
    }

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if(vkCreateCommandPool(device, &poolInfo, nullptr, &g_R.commandPool) !=
       VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo = {};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = g_R.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if(vkAllocateCommandBuffers(device, &commandInfo, &g_R.commandBuffer) !=
       VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    // Persistent host-visible staging for draw uploads and readback.
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = kVertexStagingInitial;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(vkCreateBuffer(device, &bufferInfo, nullptr, &g_R.vertexStaging) !=
       VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }
    vkGetBufferMemoryRequirements(device, g_R.vertexStaging, &requirements);
    if(!AllocateDeviceMemory(&g_R.vertexStagingMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
       vkBindBufferMemory(device, g_R.vertexStaging, g_R.vertexStagingMemory,
                          0) != VK_SUCCESS ||
       vkMapMemory(device, g_R.vertexStagingMemory, 0, kVertexStagingInitial,
                   0, &g_R.vertexMapped) != VK_SUCCESS)
    {
        printf("VULKAN| renderer vertex staging failed\n");
        RendererShutdown();
        return false;
    }
    g_R.vertexStagingSize = kVertexStagingInitial;

    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(g_R.width) * g_R.height * 4;
    bufferInfo.size = readbackSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(vkCreateBuffer(device, &bufferInfo, nullptr, &g_R.readbackStaging) !=
       VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }
    vkGetBufferMemoryRequirements(device, g_R.readbackStaging, &requirements);
    if(!AllocateDeviceMemory(&g_R.readbackMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
       vkBindBufferMemory(device, g_R.readbackStaging, g_R.readbackMemory, 0) !=
           VK_SUCCESS ||
       vkMapMemory(device, g_R.readbackMemory, 0, readbackSize, 0,
                   &g_R.readbackMapped) != VK_SUCCESS)
    {
        printf("VULKAN| renderer readback staging failed\n");
        RendererShutdown();
        return false;
    }

    VkShaderModuleCreateInfo shaderInfo = {};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.pCode = kVertexShaderSpirv;
    shaderInfo.codeSize = sizeof(kVertexShaderSpirv);
    if(vkCreateShaderModule(device, &shaderInfo, nullptr,
                            &g_R.vertexShader) != VK_SUCCESS)
    {
        printf("VULKAN| vertex shader module rejected\n");
        RendererShutdown();
        return false;
    }
    shaderInfo.pCode = kFragmentShaderSpirv;
    shaderInfo.codeSize = sizeof(kFragmentShaderSpirv);
    if(vkCreateShaderModule(device, &shaderInfo, nullptr,
                            &g_R.fragmentShader) != VK_SUCCESS)
    {
        printf("VULKAN| fragment shader module rejected\n");
        RendererShutdown();
        return false;
    }

    // Push constants: vec4 viewport + uvec4 stageOp[4] (80 bytes), shared
    // by the vertex (viewport) and fragment (stage cascade) stages.
    VkPushConstantRange pushRanges[2] = {};
    pushRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRanges[0].offset = 0;
    pushRanges[0].size = 16;
    pushRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRanges[1].offset = 16;
    pushRanges[1].size = 64;

    VkDescriptorSetLayoutBinding textureBinding = {};
    textureBinding.binding = 0;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = 4;
    textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = 1;
    layoutCreateInfo.pBindings = &textureBinding;
    if(vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr,
                                   &g_R.descriptorLayout) != VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4 * kMaxDescriptorSets;
    VkDescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = kMaxDescriptorSets;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;
    if(vkCreateDescriptorPool(device, &poolCreateInfo, nullptr,
                              &g_R.descriptorPool) != VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &g_R.descriptorLayout;
    layoutInfo.pushConstantRangeCount = 2;
    layoutInfo.pPushConstantRanges = pushRanges;
    if(vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                              &g_R.pipelineLayout) != VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    // Transition the target to GENERAL once; it stays there for attachment,
    // clear, and copy uses, which keeps the P2 frame model barrier-free.
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    vkBeginCommandBuffer(g_R.commandBuffer, &begin);
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT |
                            VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = g_R.target;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    if(vkEndCommandBuffer(g_R.commandBuffer) != VK_SUCCESS)
    {
        printf("VULKAN| renderer layout barrier recording failed\n");
        RendererShutdown();
        return false;
    }
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_R.commandBuffer;
    if(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
       vkQueueWaitIdle(queue) != VK_SUCCESS)
    {
        printf("VULKAN| renderer layout barrier submit failed\n");
        RendererShutdown();
        return false;
    }

    g_R.valid = true;

    // White 1x1 dummy for unbound texture stages.
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        imageInfo.extent = { 1, 1, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage dummyImage = VK_NULL_HANDLE;
        VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
        if(vkCreateImage(device, &imageInfo, nullptr, &dummyImage) ==
               VK_SUCCESS &&
           EnsureTextureStaging(4))
        {
            VkMemoryRequirements requirements = {};
            vkGetImageMemoryRequirements(device, dummyImage, &requirements);
            if(AllocateDeviceMemory(&dummyMemory, requirements.size,
                                    requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
               vkBindImageMemory(device, dummyImage, dummyMemory, 0) ==
                   VK_SUCCESS)
            {
                memset(g_R.textureMapped, 0xFF, 4);
                VkCommandBufferBeginInfo beginInfo = {};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags =
                    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkResetCommandBuffer(g_R.commandBuffer, 0);
                vkBeginCommandBuffer(g_R.commandBuffer, &beginInfo);
                VkImageMemoryBarrier toDst = {};
                toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.image = dummyImage;
                toDst.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                toDst.subresourceRange.levelCount = 1;
                toDst.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(g_R.commandBuffer,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &toDst);
                VkBufferImageCopy region = {};
                region.imageSubresource.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = { 1, 1, 1 };
                vkCmdCopyBufferToImage(g_R.commandBuffer, g_R.textureStaging,
                                       dummyImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &region);
                VkImageMemoryBarrier toShader = toDst;
                toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(g_R.commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1,
                                     &toShader);
                vkEndCommandBuffer(g_R.commandBuffer);
                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &g_R.commandBuffer;
                vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
                vkQueueWaitIdle(queue);

                VkImageViewCreateInfo viewInfo = {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = dummyImage;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
                viewInfo.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.layerCount = 1;
                if(vkCreateImageView(device, &viewInfo, nullptr,
                                     &g_R.dummyView) == VK_SUCCESS)
                {
                    // Ownership moves to the renderer state.
                    g_R.textures[kMaxTextures - 1].image = dummyImage;
                    g_R.textures[kMaxTextures - 1].memory = dummyMemory;
                    g_R.textures[kMaxTextures - 1].width = 1;
                    g_R.textures[kMaxTextures - 1].height = 1;
                    g_R.textureCount = kMaxTextures - 1 < g_R.textureCount
                                           ? g_R.textureCount
                                           : g_R.textureCount;
                }
            }
        }
        if(g_R.dummyView == VK_NULL_HANDLE)
        {
            if(dummyImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(device, dummyImage, nullptr);
            }
            if(dummyMemory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, dummyMemory, nullptr);
            }
            printf("VULKAN| white dummy texture unavailable; unbound stages "
                   "will sample black\n");
        }
    }

    printf("VULKAN| renderer ready: target %ux%u B8G8R8A8\n", g_R.width,
           g_R.height);
    return true;
}

void RendererShutdown()
{
    if(g_R.device == VK_NULL_HANDLE)
    {
        g_R = {};
        return;
    }
    vkDeviceWaitIdle(g_R.device);
    for(auto& sampler : g_R.samplers)
    {
        vkDestroySampler(g_R.device, sampler.second, nullptr);
    }
    g_R.samplers.clear();
    if(g_R.dummyView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(g_R.device, g_R.dummyView, nullptr);
        g_R.dummyView = VK_NULL_HANDLE;
    }
    if(g_R.dummyImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(g_R.device, g_R.dummyImage, nullptr);
        g_R.dummyImage = VK_NULL_HANDLE;
    }
    if(g_R.dummyMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.dummyMemory, nullptr);
        g_R.dummyMemory = VK_NULL_HANDLE;
    }
    if(g_R.textureMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.textureStagingMemory);
        g_R.textureMapped = nullptr;
    }
    if(g_R.textureStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.textureStaging, nullptr);
        g_R.textureStaging = VK_NULL_HANDLE;
    }
    if(g_R.textureStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.textureStagingMemory, nullptr);
        g_R.textureStagingMemory = VK_NULL_HANDLE;
    }
    g_R.textureStagingSize = 0;
    for(unsigned int i = 0; i < kMaxTextures; ++i)
    {
        DestroyTextureEntry(&g_R.textures[i]);
    }
    g_R.textureCount = 0;
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        g_R.stageTexture[stage] = nullptr;
    }
    if(g_R.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(g_R.device, g_R.descriptorPool, nullptr);
        g_R.descriptorPool = VK_NULL_HANDLE;
    }
    if(g_R.descriptorLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(g_R.device, g_R.descriptorLayout,
                                     nullptr);
        g_R.descriptorLayout = VK_NULL_HANDLE;
    }
    for(auto& entry : g_R.pipelines)
    {
        vkDestroyPipeline(g_R.device, entry.second, nullptr);
    }
    g_R.pipelines.clear();
    if(g_R.pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(g_R.device, g_R.pipelineLayout, nullptr);
        g_R.pipelineLayout = VK_NULL_HANDLE;
    }
    if(g_R.vertexShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(g_R.device, g_R.vertexShader, nullptr);
        g_R.vertexShader = VK_NULL_HANDLE;
    }
    if(g_R.fragmentShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(g_R.device, g_R.fragmentShader, nullptr);
        g_R.fragmentShader = VK_NULL_HANDLE;
    }
    if(g_R.vertexMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.vertexStagingMemory);
        g_R.vertexMapped = nullptr;
    }
    if(g_R.vertexStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.vertexStaging, nullptr);
        g_R.vertexStaging = VK_NULL_HANDLE;
    }
    if(g_R.vertexStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.vertexStagingMemory, nullptr);
        g_R.vertexStagingMemory = VK_NULL_HANDLE;
    }
    g_R.vertexStagingSize = 0;
    if(g_R.readbackMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.readbackMemory);
        g_R.readbackMapped = nullptr;
    }
    if(g_R.readbackStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.readbackStaging, nullptr);
        g_R.readbackStaging = VK_NULL_HANDLE;
    }
    if(g_R.readbackMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.readbackMemory, nullptr);
        g_R.readbackMemory = VK_NULL_HANDLE;
    }
    if(g_R.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(g_R.device, g_R.commandPool, nullptr);
        g_R.commandPool = VK_NULL_HANDLE;
        g_R.commandBuffer = VK_NULL_HANDLE;
    }
    if(g_R.targetView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(g_R.device, g_R.targetView, nullptr);
        g_R.targetView = VK_NULL_HANDLE;
    }
    if(g_R.target != VK_NULL_HANDLE)
    {
        vkDestroyImage(g_R.device, g_R.target, nullptr);
        g_R.target = VK_NULL_HANDLE;
    }
    if(g_R.targetMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.targetMemory, nullptr);
        g_R.targetMemory = VK_NULL_HANDLE;
    }
    g_R.valid = false;
    g_R.frameOpen = false;
    g_R.renderingActive = false;
    DeleteCriticalSection(&g_RendererLock);
}

bool RendererValid()
{
    return g_R.valid;
}

void RendererSetViewport(float x, float y, float width, float height)
{
    RendererLockScope rendererLock;
    g_R.viewport[0] = x;
    g_R.viewport[1] = y;
    g_R.viewport[2] = width != 0.0f ? width : static_cast<float>(g_R.width);
    g_R.viewport[3] = height != 0.0f ? height : static_cast<float>(g_R.height);
}

bool RendererClear(unsigned int flags, unsigned int color)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || flags == 0)
    {
        return false;
    }
    if(!OpenFrame())
    {
        g_R.valid = false;
        return false;
    }
    CloseRendering();

    // X_D3DCOLOR 0xAARRGGBB into RGBA channel order: VkClearColorValue
    // components are channel roles (R, G, B, A), not format byte order. The
    // clear rides on the next rendering instance's loadOp so it cannot be
    // lost against the draw batch or reordered ahead of a submit.
    g_R.pendingClear[0] = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    g_R.pendingClear[1] = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    g_R.pendingClear[2] = static_cast<float>(color & 0xFF) / 255.0f;
    g_R.pendingClear[3] = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    g_R.hasPendingClear = true;
    return true;
}

bool RendererDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                    const void* data, unsigned int stride,
                    unsigned int diffuseOffset, unsigned int texCoordOffset)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || data == nullptr || primitiveCount == 0 || stride == 0)
    {
        return false;
    }
    // DrawPrimitiveUP semantics: primitive count -> vertex count.
    unsigned int vertexCount = 0;
    switch(primitiveType)
    {
        case kPointList:
            vertexCount = primitiveCount;
            break;
        case kLineList:
            vertexCount = primitiveCount * 2;
            break;
        case kLineStrip:
            vertexCount = primitiveCount + 1;
            break;
        case kTriangleList:
            vertexCount = primitiveCount * 3;
            break;
        case kTriangleStrip:
        case kTriangleFan:
            vertexCount = primitiveCount + 2;
            break;
        default:
            return false;
    }
    if((stride < 16) || (diffuseOffset != kNoDiffuse && diffuseOffset + 4 > stride))
    {
        if(!g_R.unsupportedLogged)
        {
            g_R.unsupportedLogged = true;
            printf("VULKAN| renderer: unsupported draw layout (stride=%u "
                   "diffuse=%u); draw dropped\n",
                   stride, diffuseOffset);
        }
        return false;
    }

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(stride) * vertexCount;
    if(!OpenFrame())
    {
        g_R.valid = false;
        return false;
    }
    const VkDeviceSize offset = g_R.vertexCursor;
    if(offset + bytes > g_R.vertexStagingSize)
    {
        // P2 keeps the staging buffer at a fixed generous size; oversized
        // draws are dropped loudly instead of growing mid-frame.
        printf("VULKAN| renderer: draw exceeds vertex staging (%llu bytes)\n",
               static_cast<unsigned long long>(bytes));
        return false;
    }
    memcpy(static_cast<char*>(g_R.vertexMapped) + offset, data,
           static_cast<size_t>(bytes));
    g_R.vertexCursor = offset + bytes;

    if(!g_R.renderingActive)
    {
        VkRenderingAttachmentInfo attachment = {};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = g_R.targetView;
        attachment.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        attachment.loadOp = g_R.hasPendingClear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if(g_R.hasPendingClear)
        {
            memcpy(attachment.clearValue.color.float32, g_R.pendingClear,
                   sizeof(g_R.pendingClear));
            g_R.hasPendingClear = false;
        }
        VkRenderingInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = { g_R.width, g_R.height };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(g_R.commandBuffer, &rendering);
        g_R.renderingActive = true;
    }

    VkPipeline pipeline = PipelineFor(primitiveType, stride, diffuseOffset,
                                      texCoordOffset);
    if(pipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    vkCmdBindPipeline(g_R.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
    VkViewport viewport = { g_R.viewport[0], g_R.viewport[1], g_R.viewport[2],
                            g_R.viewport[3], 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { g_R.width, g_R.height } };
    vkCmdSetViewport(g_R.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(g_R.commandBuffer, 0, 1, &scissor);
    vkCmdPushConstants(g_R.commandBuffer, g_R.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, 16, g_R.viewport);

    // Bind the four stage textures (white dummy for unbound stages) through
    // a fresh descriptor set; the pool resets after every frame submit.
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = g_R.descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &g_R.descriptorLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if(vkAllocateDescriptorSets(g_R.device, &allocateInfo, &set) !=
       VK_SUCCESS)
    {
        printf("VULKAN| descriptor set allocation failed\n");
        return false;
    }
    VkDescriptorImageInfo imageInfos[4] = {};
    VkWriteDescriptorSet writes[4] = {};
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        RendererTexture* texture = g_R.stageTexture[stage];
        imageInfos[stage].sampler = SamplerFor(stage);
        imageInfos[stage].imageView =
            texture != nullptr ? texture->view : g_R.dummyView;
        imageInfos[stage].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[stage].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[stage].dstSet = set;
        writes[stage].dstBinding = 0;
        writes[stage].dstArrayElement = stage;
        writes[stage].descriptorCount = 1;
        writes[stage].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[stage].pImageInfo = &imageInfos[stage];
    }
    vkUpdateDescriptorSets(g_R.device, 4, writes, 0, nullptr);
    vkCmdBindDescriptorSets(g_R.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_R.pipelineLayout, 0, 1, &set, 0, nullptr);

    std::uint32_t stageConstants[16] = {};
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        stageConstants[stage * 4 + 0] = g_R.stageOp[stage];
        stageConstants[stage * 4 + 1] = g_R.stageArg1[stage];
        stageConstants[stage * 4 + 2] = g_R.stageArg2[stage];
    }
    vkCmdPushConstants(g_R.commandBuffer, g_R.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 16, 64, stageConstants);
    VkDeviceSize bufferOffset = offset;
    vkCmdBindVertexBuffers(g_R.commandBuffer, 0, 1, &g_R.vertexStaging,
                           &bufferOffset);

    unsigned int drawCount = vertexCount;
    if(primitiveType == kTriangleList || primitiveType == kTriangleStrip ||
       primitiveType == kTriangleFan)
    {
        drawCount = vertexCount; // vertices, not primitives
    }
    vkCmdDraw(g_R.commandBuffer, drawCount, 1, 0, 0);
    return true;
}

unsigned int RendererTargetWidth()
{
    return g_R.width;
}

unsigned int RendererTargetHeight()
{
    return g_R.height;
}

bool RendererSetTexture(unsigned int stage, void* key, const void* pixels,
                        unsigned int pitch, unsigned int width,
                        unsigned int height, unsigned int hostFormat)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || stage >= 4)
    {
        return false;
    }
    if(key == nullptr || pixels == nullptr || width == 0 || height == 0)
    {
        g_R.stageTexture[stage] = nullptr;
        return true;
    }

    VkFormat format = VK_FORMAT_UNDEFINED;
    const unsigned int bytesPerPixel =
        TextureFormatBytesPerPixel(hostFormat, &format);
    if(bytesPerPixel == 0)
    {
        if(!g_R.textureFormatLogged)
        {
            g_R.textureFormatLogged = true;
            printf("VULKAN| texture host format %u not mirrored yet; stage "
                   "samples white\n",
                   hostFormat);
        }
        g_R.stageTexture[stage] = nullptr;
        return false;
    }
    if(width > 4096 || height > 4096 ||
       static_cast<VkDeviceSize>(pitch) * height > 16 * 1024 * 1024)
    {
        printf("VULKAN| texture %ux%u exceeds the P3 upload budget\n", width,
               height);
        g_R.stageTexture[stage] = nullptr;
        return false;
    }

    RendererTexture* entry = nullptr;
    for(unsigned int i = 0; i < g_R.textureCount; ++i)
    {
        if(g_R.textures[i].key == key)
        {
            entry = &g_R.textures[i];
            break;
        }
    }
    if(entry != nullptr &&
       (entry->width != width || entry->height != height ||
        entry->hostFormat != hostFormat))
    {
        DestroyTextureEntry(entry);
        entry = nullptr;
    }
    if(entry == nullptr)
    {
        if(g_R.textureCount == kMaxTextures)
        {
            // Evict the least recently used entry (images may be referenced
            // by the pending batch; the idle wait below keeps it safe).
            unsigned int oldest = 0;
            for(unsigned int i = 1; i < g_R.textureCount; ++i)
            {
                if(g_R.textures[i].lastUse < g_R.textures[oldest].lastUse)
                {
                    oldest = i;
                }
            }
            SubmitFrame();
            DestroyTextureEntry(&g_R.textures[oldest]);
            entry = &g_R.textures[oldest];
        }
        else
        {
            entry = &g_R.textures[g_R.textureCount++];
        }
        entry->key = key;
        entry->width = width;
        entry->height = height;
        entry->hostFormat = hostFormat;

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if(vkCreateImage(g_R.device, &imageInfo, nullptr, &entry->image) !=
           VK_SUCCESS)
        {
            DestroyTextureEntry(entry);
            g_R.textureCount--;
            g_R.stageTexture[stage] = nullptr;
            return false;
        }
        VkMemoryRequirements requirements = {};
        vkGetImageMemoryRequirements(g_R.device, entry->image, &requirements);
        if(!AllocateDeviceMemory(&entry->memory, requirements.size,
                                 requirements.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
           vkBindImageMemory(g_R.device, entry->image, entry->memory, 0) !=
               VK_SUCCESS)
        {
            DestroyTextureEntry(entry);
            g_R.textureCount--;
            g_R.stageTexture[stage] = nullptr;
            return false;
        }
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = entry->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if(vkCreateImageView(g_R.device, &viewInfo, nullptr, &entry->view) !=
           VK_SUCCESS)
        {
            DestroyTextureEntry(entry);
            g_R.textureCount--;
            g_R.stageTexture[stage] = nullptr;
            return false;
        }
        entry->lastUse = ++g_R.textureUseCounter;
    }

    // Tight row copy into the upload staging, then buffer->image.
    if(!EnsureTextureStaging(static_cast<VkDeviceSize>(width) * height *
                             bytesPerPixel))
    {
        g_R.stageTexture[stage] = nullptr;
        return false;
    }
    const unsigned tightPitch = width * bytesPerPixel;
    for(unsigned row = 0; row < height; ++row)
    {
        memcpy(static_cast<char*>(g_R.textureMapped) +
                   static_cast<size_t>(row) * tightPitch,
               static_cast<const char*>(pixels) +
                   static_cast<size_t>(row) * pitch,
               tightPitch);
    }

    if(!OpenFrame())
    {
        g_R.valid = false;
        g_R.stageTexture[stage] = nullptr;
        return false;
    }
    VkImageMemoryBarrier toDst = {};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = entry->image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    VkBufferImageCopy region = {};
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(g_R.commandBuffer, g_R.textureStaging, entry->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier toShader = toDst;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toShader);
    entry->lastUse = ++g_R.textureUseCounter;
    g_R.stageTexture[stage] = entry;
    return true;
}

void RendererSetTextureOp(unsigned int stage, unsigned int type,
                          unsigned int value)
{
    RendererLockScope rendererLock;
    if(stage >= 4)
    {
        return;
    }
    switch(type)
    {
        case 0:
            g_R.stageOp[stage] = value;
            break;
        case 1:
            g_R.stageArg1[stage] = value;
            break;
        case 2:
            g_R.stageArg2[stage] = value;
            break;
        default:
            break;
    }
}

void RendererSetSamplerState(unsigned int stage, unsigned int type,
                             unsigned int value)
{
    RendererLockScope rendererLock;
    if(stage >= 4)
    {
        return;
    }
    switch(type)
    {
        case 0:
            g_R.stageAddressU[stage] = value;
            break;
        case 1:
            g_R.stageAddressV[stage] = value;
            break;
        case 2:
            g_R.stageMagFilter[stage] = value;
            break;
        case 3:
            g_R.stageMinFilter[stage] = value;
            break;
        default:
            break;
    }
}

bool RendererHasPendingFrame()
{
    RendererLockScope rendererLock;
    return g_R.frameOpen;
}

bool RendererEndFrameForPresent()
{
    RendererLockScope rendererLock;
    return SubmitFrame();
}

bool RendererReadTarget(void* dst, unsigned int pitch)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || dst == nullptr)
    {
        return false;
    }
    if(!SubmitFrame())
    {
        g_R.valid = false;
        return false;
    }
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    if(vkBeginCommandBuffer(g_R.commandBuffer, &begin) != VK_SUCCESS)
    {
        return false;
    }
    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { g_R.width, g_R.height, 1 };
    vkCmdCopyImageToBuffer(g_R.commandBuffer, g_R.target,
                           VK_IMAGE_LAYOUT_GENERAL, g_R.readbackStaging, 1,
                           &region);
    vkEndCommandBuffer(g_R.commandBuffer);
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_R.commandBuffer;
    if(vkQueueSubmit(g_R.queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        g_R.valid = false;
        return false;
    }
    vkQueueWaitIdle(g_R.queue);
    for(unsigned int row = 0; row < g_R.height; ++row)
    {
        memcpy(static_cast<char*>(dst) + static_cast<size_t>(row) * pitch,
               static_cast<const char*>(g_R.readbackMapped) +
                   static_cast<size_t>(row) * g_R.width * 4,
               g_R.width * 4);
    }
    return true;
}

bool RendererCopyToSwapchain(std::uint64_t swapchainImageHandle,
                             unsigned int imageWidth, unsigned int imageHeight)
{
    RendererLockScope rendererLock;
    const VkImage swapchainImage = static_cast<VkImage>(swapchainImageHandle);
    if(!g_R.valid || swapchainImage == VK_NULL_HANDLE)
    {
        return false;
    }
    if(!SubmitFrame())
    {
        g_R.valid = false;
        return false;
    }
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    if(vkBeginCommandBuffer(g_R.commandBuffer, &begin) != VK_SUCCESS)
    {
        g_R.valid = false;
        return false;
    }

    const unsigned int copyWidth =
        imageWidth < g_R.width ? imageWidth : g_R.width;
    const unsigned int copyHeight =
        imageHeight < g_R.height ? imageHeight : g_R.height;

    VkImageMemoryBarrier toDst = {};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = swapchainImage;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = { copyWidth, copyHeight, 1 };
    vkCmdCopyImage(g_R.commandBuffer, g_R.target, VK_IMAGE_LAYOUT_GENERAL,
                   swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region);

    VkImageMemoryBarrier toPresent = toDst;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toPresent);
    vkEndCommandBuffer(g_R.commandBuffer);
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_R.commandBuffer;
    if(vkQueueSubmit(g_R.queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        g_R.valid = false;
        return false;
    }
    vkQueueWaitIdle(g_R.queue);
    return true;
}

void RendererShutdownAfterDeviceLoss()
{
    g_R.valid = false;
}

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx
