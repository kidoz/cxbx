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
#include <vector>

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
// uint16 indices: 512k indices per frame before the draw drops loudly.
constexpr unsigned int kIndexStagingInitial = 256 * 1024;
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
    float pendingClearZ = 1.0f;
    unsigned int pendingClearStencil = 0;
    bool hasPendingClearZ = false;
    // d3d8 depth-test state (ZEnable / ZWriteEnable / ZFUNC); the compare
    // value is the host D3DCMPFUNC enumeration.
    bool depthEnable = false;
    bool depthWrite = true;
    unsigned int depthFunc = 4; // D3DCMP_LESSEQUAL
    // True when recorded-but-not-yet-synchronized GPU work has written a
    // target this command buffer (finished rendering instance or direct
    // clear); the next rendering instance must emit a memory barrier so a
    // target sampled as a texture sees the writes.
    bool gpuWroteTarget = false;

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

    // P5 render-target registry: the main target (index 0, the present
    // source) plus render-to-texture targets keyed by host surface pointer.
    struct RTEntry
    {
        void* key = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        // Size-matched depth attachment (D32; Xbox D24S8 stencil lands in a
        // later phase). Created alongside the color target; the main target
        // gets one lazily when a depth-stencil surface is first bound.
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        unsigned int width = 0;
        unsigned int height = 0;
    };
    RTEntry rtTargets[8] = {};
    unsigned int rtTargetCount = 0;
    int rtCurrent = -1; // -1 = main target; else rtTargets index
    // Depth attachment for the main target (present source).
    VkImage mainDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory mainDepthMemory = VK_NULL_HANDLE;
    VkImageView mainDepthView = VK_NULL_HANDLE;
    VkImageView stageOverrideView[4] = {};

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkBuffer vertexStaging = VK_NULL_HANDLE;
    VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
    void* vertexMapped = nullptr;
    VkDeviceSize vertexStagingSize = 0;
    VkDeviceSize vertexCursor = 0;
    // Index staging for the indexed-draw path (uint16 indices, caller-pulled
    // per draw); the cursor resets with each frame like the vertex ring.
    VkBuffer indexStaging = VK_NULL_HANDLE;
    VkDeviceMemory indexStagingMemory = VK_NULL_HANDLE;
    void* indexMapped = nullptr;
    VkDeviceSize indexStagingSize = 0;
    VkDeviceSize indexCursor = 0;

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

    // P4 register-combiner config, mirrored into a persistent UBO. The
    // layout matches the CombinConfig block in shader_spirv.h (std140).
    VkBuffer combinerBuffer = VK_NULL_HANDLE;
    VkDeviceMemory combinerMemory = VK_NULL_HANDLE;
    void* combinerMapped = nullptr;
    unsigned int combinerWriteSlot = 0;  // next free ring slot
    unsigned int combinerActiveSlot = 0; // slot bound by the next draw
    unsigned int ringWraps = 0;
    std::uint32_t pixelShaderDefs[64][60] = {}; // handle-table slot storage
    bool useCombiner = false;
    std::uint32_t lastConfig[104] = {};
};

RendererState g_R;

using RTEntry = RendererState::RTEntry;

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

    // Depth state is fully dynamic (core 1.3); the static block only needs
    // to exist. Targets always carry a D32 depth attachment so pipelines
    // declare it unconditionally.
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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

    VkDynamicState dynamicStates[5] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 5;
    dynamic.pDynamicStates = dynamicStates;

    // Dynamic rendering needs the attachment formats the pipeline was built
    // for (no render pass object carries them).
    const VkFormat renderingFormats[2] = { VK_FORMAT_B8G8R8A8_UNORM,
                                           VK_FORMAT_D32_SFLOAT };
    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &renderingFormats[0];
    renderingInfo.depthAttachmentFormat = renderingFormats[1];

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = g_R.pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // dynamic rendering

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult created = vkCreateGraphicsPipelines(
        g_R.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if(created != VK_SUCCESS)
    {
        printf("VULKAN| pipeline creation failed (topology=%u stride=%u "
               "result=%d)\n",
               static_cast<unsigned>(topology), stride,
               static_cast<int>(created));
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
        g_R.gpuWroteTarget = true;
    }
}

// Memory dependency between GPU work recorded earlier in this command
// buffer (previous rendering instance or direct clear) and the following
// recording: targets rest in GENERAL, so writes must be made visible to
// shader reads (sampling) and to the next instance's attachment access.
void BarrierAfterTargetWrite()
{
    if(!g_R.gpuWroteTarget)
    {
        return;
    }
    g_R.gpuWroteTarget = false;
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Outside-rendering clear commands execute in the TRANSFER stage under
    // legacy synchronization; fragment depth writes ride the fragment-tests
    // stages.
    vkCmdPipelineBarrier(
        g_R.commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
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
    g_R.gpuWroteTarget = false;
    if(vkBeginCommandBuffer(g_R.commandBuffer, &begin) != VK_SUCCESS)
    {
        printf("VULKAN| renderer command buffer begin failed\n");
        return false;
    }
    g_R.frameOpen = true;
    g_R.vertexCursor = 0;
    g_R.indexCursor = 0;
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
        const VkImage currentImage =
            g_R.rtCurrent >= 0 ? g_R.rtTargets[g_R.rtCurrent].image
                               : g_R.target;
        vkCmdClearColorImage(g_R.commandBuffer, currentImage,
                             VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
        g_R.hasPendingClear = false;
        g_R.gpuWroteTarget = true;
    }
    if(g_R.hasPendingClearZ)
    {
        const VkImage depthImage = g_R.rtCurrent >= 0
                                       ? g_R.rtTargets[g_R.rtCurrent].depthImage
                                       : g_R.mainDepthImage;
        if(depthImage != VK_NULL_HANDLE)
        {
            VkClearDepthStencilValue depthValue = {};
            depthValue.depth = g_R.pendingClearZ;
            depthValue.stencil = g_R.pendingClearStencil;
            VkImageSubresourceRange depthRange = {};
            depthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depthRange.levelCount = 1;
            depthRange.layerCount = 1;
            vkCmdClearDepthStencilImage(
                g_R.commandBuffer, depthImage,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, &depthValue, 1,
                &depthRange);
            g_R.gpuWroteTarget = true;
        }
        g_R.hasPendingClearZ = false;
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
    g_R.combinerWriteSlot = 0;
    g_R.combinerActiveSlot = 0;
    memset(g_R.lastConfig, 0, sizeof(g_R.lastConfig));
    return true;
}

// Host-format byte size for the supported mirror formats (0 = unsupported).
// Decodes a DXT1/2/3/4/5 (BC1/BC2/BC3) level-0 image into tightly packed
// BGRA (B8G8R8A8 byte order). Titles bind block-compressed uploads (Turok
// Evolution's world textures) that the host path serves linearly through
// LockRect; the Vulkan side samples the decoded RGBA instead of mapping the
// compressed format directly (keeps the upload path format-neutral).
bool DecodeDxtToBgra(unsigned int hostFormat, const void* pixels,
                     unsigned int pitch, unsigned int width,
                     unsigned int height, std::vector<std::uint32_t>* out)
{
    unsigned int blockBytes = 0;
    unsigned int alphaMode = 0; // 0 = none, 1 = 4-bit (DXT2/3), 2 = interp (DXT4/5)
    switch(hostFormat)
    {
        case 0x31545844: blockBytes = 8; break; // 'DXT1'
        case 0x32545844:
            blockBytes = 16;
            alphaMode = 1;
            break; // 'DXT2'
        case 0x33545844:
            blockBytes = 16;
            alphaMode = 1;
            break; // 'DXT3'
        case 0x34545844:
            blockBytes = 16;
            alphaMode = 2;
            break; // 'DXT4'
        case 0x35545844:
            blockBytes = 16;
            alphaMode = 2;
            break; // 'DXT5'
        default:
            return false;
    }
    out->assign(static_cast<size_t>(width) * height, 0);
    const unsigned int blocksX = (width + 3) / 4;
    const unsigned int blocksY = (height + 3) / 4;
    const std::uint8_t* base = static_cast<const std::uint8_t*>(pixels);
    const auto expand565 = [](std::uint16_t value, std::uint8_t rgb[3])
    {
        const unsigned r = (value >> 11) & 0x1F;
        const unsigned g = (value >> 5) & 0x3F;
        const unsigned b = value & 0x1F;
        rgb[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
        rgb[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
        rgb[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    };
    for(unsigned int by = 0; by < blocksY; ++by)
    {
        for(unsigned int bx = 0; bx < blocksX; ++bx)
        {
            const std::uint8_t* block =
                base + static_cast<size_t>(by) * pitch +
                static_cast<size_t>(bx) * blockBytes;
            std::uint8_t alpha[16];
            std::fill(alpha, alpha + 16, 255);
            if(alphaMode == 1)
            {
                for(unsigned int i = 0; i < 16; ++i)
                {
                    alpha[i] = static_cast<std::uint8_t>(
                        ((block[i >> 1] >> ((i & 1) * 4)) & 0xF) * 17);
                }
            }
            else if(alphaMode == 2)
            {
                const unsigned a0 = block[0];
                const unsigned a1 = block[1];
                std::uint8_t palette[8] = {};
                palette[0] = static_cast<std::uint8_t>(a0);
                palette[1] = static_cast<std::uint8_t>(a1);
                if(a0 > a1)
                {
                    for(unsigned k = 2; k < 8; ++k)
                    {
                        palette[k] = static_cast<std::uint8_t>(
                            ((8 - k) * a0 + (k - 1) * a1) / 7);
                    }
                }
                else
                {
                    for(unsigned k = 2; k < 6; ++k)
                    {
                        palette[k] = static_cast<std::uint8_t>(
                            ((6 - k) * a0 + (k - 1) * a1) / 5);
                    }
                    palette[6] = 0;
                    palette[7] = 255;
                }
                std::uint64_t codes = 0;
                for(unsigned k = 0; k < 6; ++k)
                {
                    codes |= static_cast<std::uint64_t>(block[2 + k]) << (8 * k);
                }
                for(unsigned int i = 0; i < 16; ++i)
                {
                    alpha[i] = palette[(codes >> (3 * i)) & 7];
                }
            }
            const std::uint8_t* colorBlock =
                block + (blockBytes == 16 ? 8 : 0);
            const std::uint16_t c0 = static_cast<std::uint16_t>(
                colorBlock[0] | (colorBlock[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(
                colorBlock[2] | (colorBlock[3] << 8));
            std::uint8_t color[4][3];
            expand565(c0, color[0]);
            expand565(c1, color[1]);
            for(unsigned k = 0; k < 3; ++k)
            {
                color[2][k] = static_cast<std::uint8_t>((2 * color[0][k] +
                                                         color[1][k] + 1) /
                                                        3);
                color[3][k] = static_cast<std::uint8_t>((color[0][k] +
                                                         2 * color[1][k] + 1) /
                                                        3);
            }
            const bool threeColorMode = (blockBytes == 8 && c0 <= c1);
            std::uint32_t indices = 0;
            for(unsigned k = 0; k < 4; ++k)
            {
                indices |= static_cast<std::uint32_t>(colorBlock[4 + k]) << (8 * k);
            }
            for(unsigned int i = 0; i < 16; ++i)
            {
                const unsigned int x = bx * 4 + (i & 3);
                const unsigned int y = by * 4 + (i >> 2);
                if(x >= width || y >= height)
                {
                    continue;
                }
                const unsigned index = (indices >> (2 * i)) & 3;
                std::uint8_t a = alpha[i];
                if(threeColorMode && index == 3)
                {
                    a = 0;
                }
                const std::uint8_t* rgb = color[index];
                (*out)[static_cast<size_t>(y) * width + x] =
                    (static_cast<std::uint32_t>(a) << 24) |
                    (static_cast<std::uint32_t>(rgb[0]) << 16) |
                    (static_cast<std::uint32_t>(rgb[1]) << 8) | rgb[2];
            }
        }
    }
    return true;
}

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

// Creates a GENERAL-layout BGRA color image (renderable + sampleable) and
// transitions it in a one-shot submit. Used for render-to-texture targets.
// Creates one D32 depth attachment with a one-shot transition to
// DEPTH_ATTACHMENT_OPTIMAL and a far-plane clear, so draws that never clear
// depth test deterministically against the far plane (matching the host
// d3d8 path's backbuffer depth behavior).
bool CreateDepthAttachment(unsigned int width, unsigned int height,
                           VkImage* outImage, VkDeviceMemory* outMemory,
                           VkImageView* outView)
{
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(g_R.device, &imageInfo, nullptr, outImage) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(g_R.device, *outImage, &requirements);
    if(!AllocateDeviceMemory(outMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
       vkBindImageMemory(g_R.device, *outImage, *outMemory, 0) != VK_SUCCESS)
    {
        vkDestroyImage(g_R.device, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if(vkCreateImageView(g_R.device, &viewInfo, nullptr, outView) !=
       VK_SUCCESS)
    {
        vkDestroyImage(g_R.device, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }

    // One-shot transition + deterministic far-plane clear.
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    vkBeginCommandBuffer(g_R.commandBuffer, &beginInfo);
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *outImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    VkClearDepthStencilValue clearValue = { 1.0f, 0 };
    VkImageSubresourceRange clearRange = {};
    clearRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clearRange.levelCount = 1;
    clearRange.layerCount = 1;
    vkCmdClearDepthStencilImage(g_R.commandBuffer, *outImage,
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                &clearValue, 1, &clearRange);
    vkEndCommandBuffer(g_R.commandBuffer);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_R.commandBuffer;
    vkQueueSubmit(g_R.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_R.queue);
    return true;
}

bool CreateRenderableTarget(void* key, unsigned int width, unsigned int height)
{
    if(g_R.rtTargetCount >= 8)
    {
        return false;
    }
    RTEntry& entry = g_R.rtTargets[g_R.rtTargetCount];
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(g_R.device, &imageInfo, nullptr, &entry.image) !=
       VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(g_R.device, entry.image, &requirements);
    if(!AllocateDeviceMemory(&entry.memory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
       vkBindImageMemory(g_R.device, entry.image, entry.memory, 0) !=
           VK_SUCCESS)
    {
        vkDestroyImage(g_R.device, entry.image, nullptr);
        entry.image = VK_NULL_HANDLE;
        return false;
    }
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = entry.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if(vkCreateImageView(g_R.device, &viewInfo, nullptr, &entry.view) !=
       VK_SUCCESS)
    {
        vkDestroyImage(g_R.device, entry.image, nullptr);
        entry.image = VK_NULL_HANDLE;
        return false;
    }

    // One-shot transition to GENERAL (attachment + sampled + copy source).
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g_R.commandBuffer, 0);
    vkBeginCommandBuffer(g_R.commandBuffer, &beginInfo);
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = entry.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_R.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    vkEndCommandBuffer(g_R.commandBuffer);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_R.commandBuffer;
    vkQueueSubmit(g_R.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_R.queue);

    entry.key = key;
    entry.width = width;
    entry.height = height;
    if(!CreateDepthAttachment(width, height, &entry.depthImage,
                              &entry.depthMemory, &entry.depthView))
    {
        printf("VULKAN| render target depth creation failed; keeping main\n");
    }
    g_R.rtTargetCount++;
    return true;
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

// Index staging for the indexed-draw path: same host-visible staging model
// as the vertex ring, but VK_INDEX_BUFFER_USAGE and uint16 entries.
bool EnsureIndexStaging(VkDeviceSize needed)
{
    if(g_R.indexStagingSize >= needed)
    {
        return true;
    }
    if(g_R.indexMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.indexStagingMemory);
        g_R.indexMapped = nullptr;
    }
    if(g_R.indexStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.indexStaging, nullptr);
        g_R.indexStaging = VK_NULL_HANDLE;
    }
    if(g_R.indexStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.indexStagingMemory, nullptr);
        g_R.indexStagingMemory = VK_NULL_HANDLE;
    }
    g_R.indexStagingSize = 0;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = needed;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(vkCreateBuffer(g_R.device, &bufferInfo, nullptr, &g_R.indexStaging) !=
       VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(g_R.device, g_R.indexStaging,
                                  &requirements);
    if(!AllocateDeviceMemory(&g_R.indexStagingMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        return false;
    }
    if(vkBindBufferMemory(g_R.device, g_R.indexStaging,
                          g_R.indexStagingMemory, 0) != VK_SUCCESS)
    {
        return false;
    }
    if(vkMapMemory(g_R.device, g_R.indexStagingMemory, 0, needed, 0,
                   &g_R.indexMapped) != VK_SUCCESS)
    {
        return false;
    }
    g_R.indexStagingSize = needed;
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

    bufferInfo.size = kIndexStagingInitial;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(vkCreateBuffer(device, &bufferInfo, nullptr, &g_R.indexStaging) !=
       VK_SUCCESS)
    {
        printf("VULKAN| renderer index staging failed\n");
        RendererShutdown();
        return false;
    }
    vkGetBufferMemoryRequirements(device, g_R.indexStaging, &requirements);
    if(!AllocateDeviceMemory(&g_R.indexStagingMemory, requirements.size,
                             requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
       vkBindBufferMemory(device, g_R.indexStaging, g_R.indexStagingMemory,
                          0) != VK_SUCCESS ||
       vkMapMemory(device, g_R.indexStagingMemory, 0, kIndexStagingInitial, 0,
                   &g_R.indexMapped) != VK_SUCCESS)
    {
        printf("VULKAN| renderer index staging failed\n");
        RendererShutdown();
        return false;
    }
    g_R.indexStagingSize = kIndexStagingInitial;

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
    pushRanges[1].size = 80; // stageOp[4] + extra (useCombiner)

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 4;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = 2;
    layoutCreateInfo.pBindings = bindings;
    if(vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr,
                                   &g_R.descriptorLayout) != VK_SUCCESS)
    {
        RendererShutdown();
        return false;
    }

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 4 * kMaxDescriptorSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[1].descriptorCount = kMaxDescriptorSets;
    VkDescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = kMaxDescriptorSets;
    poolCreateInfo.poolSizeCount = 2;
    poolCreateInfo.pPoolSizes = poolSizes;
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

    // Persistent combiner-config UBO (binding 1). The descriptor write for
    // it happens once; contents update through the mapped pointer.
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = 8192;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if(vkCreateBuffer(device, &bufferInfo, nullptr,
                          &g_R.combinerBuffer) == VK_SUCCESS)
        {
            VkMemoryRequirements requirements = {};
            vkGetBufferMemoryRequirements(device, g_R.combinerBuffer,
                                          &requirements);
            if(AllocateDeviceMemory(&g_R.combinerMemory, requirements.size,
                                    requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
               vkBindBufferMemory(device, g_R.combinerBuffer,
                                  g_R.combinerMemory, 0) == VK_SUCCESS &&
               vkMapMemory(device, g_R.combinerMemory, 0, 8192, 0,
                           &g_R.combinerMapped) == VK_SUCCESS)
            {
                memset(g_R.combinerMapped, 0, 8192);
                static_cast<std::uint32_t*>(g_R.combinerMapped)[96] = 1;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[32] = 0x08040000;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[33] = 0x18140000;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[34] = 0x00000C00;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[35] = 0x00000C00;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[28] = 0x3F800000;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[29] = 0;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[30] = 0x3F800000;
                static_cast<std::uint32_t*>(g_R.combinerMapped)[31] = 0x3F800000;
            }
            else
            {
                printf("VULKAN| combiner UBO unavailable; pixel shaders "
                       "sample nothing\n");
            }
        }
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

    // Pipelines declare a D32 depth attachment unconditionally, so the main
    // target gets its depth image even when nothing depth-tests yet.
    if(!CreateDepthAttachment(g_R.width, g_R.height, &g_R.mainDepthImage,
                              &g_R.mainDepthMemory, &g_R.mainDepthView))
    {
        printf("VULKAN| renderer main depth attachment failed\n");
        RendererShutdown();
        return false;
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
    for(unsigned int i = 0; i < g_R.rtTargetCount; ++i)
    {
        RTEntry& entry = g_R.rtTargets[i];
        if(entry.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(g_R.device, entry.view, nullptr);
        }
        if(entry.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(g_R.device, entry.image, nullptr);
        }
        if(entry.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(g_R.device, entry.memory, nullptr);
        }
        if(entry.depthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(g_R.device, entry.depthView, nullptr);
        }
        if(entry.depthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(g_R.device, entry.depthImage, nullptr);
        }
        if(entry.depthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(g_R.device, entry.depthMemory, nullptr);
        }
    }
    g_R.rtTargetCount = 0;
    g_R.rtCurrent = -1;
    if(g_R.mainDepthView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(g_R.device, g_R.mainDepthView, nullptr);
        g_R.mainDepthView = VK_NULL_HANDLE;
    }
    if(g_R.mainDepthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(g_R.device, g_R.mainDepthImage, nullptr);
        g_R.mainDepthImage = VK_NULL_HANDLE;
    }
    if(g_R.mainDepthMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.mainDepthMemory, nullptr);
        g_R.mainDepthMemory = VK_NULL_HANDLE;
    }
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        g_R.stageOverrideView[stage] = VK_NULL_HANDLE;
    }
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
    if(g_R.combinerMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.combinerMemory);
        g_R.combinerMapped = nullptr;
    }
    if(g_R.combinerBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.combinerBuffer, nullptr);
        g_R.combinerBuffer = VK_NULL_HANDLE;
    }
    if(g_R.combinerMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.combinerMemory, nullptr);
        g_R.combinerMemory = VK_NULL_HANDLE;
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
    if(g_R.indexMapped != nullptr)
    {
        vkUnmapMemory(g_R.device, g_R.indexStagingMemory);
        g_R.indexMapped = nullptr;
    }
    if(g_R.indexStaging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_R.device, g_R.indexStaging, nullptr);
        g_R.indexStaging = VK_NULL_HANDLE;
    }
    if(g_R.indexStagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_R.device, g_R.indexStagingMemory, nullptr);
        g_R.indexStagingMemory = VK_NULL_HANDLE;
    }
    g_R.indexStagingSize = 0;
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

bool RendererClear(unsigned int flags, unsigned int color, float z,
                   unsigned int stencil)
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

    // PC D3DCLEAR flag subset: bit 0 = target, bit 1 = z, bit 2 = stencil.
    // X_D3DCOLOR 0xAARRGGBB into RGBA channel order: VkClearColorValue
    // components are channel roles (R, G, B, A), not format byte order. The
    // clears ride on the next rendering instance's loadOp so they cannot be
    // lost against the draw batch or reordered ahead of a submit.
    if((flags & 0x1) != 0)
    {
        g_R.pendingClear[0] = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        g_R.pendingClear[1] = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        g_R.pendingClear[2] = static_cast<float>(color & 0xFF) / 255.0f;
        g_R.pendingClear[3] = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        g_R.hasPendingClear = true;
    }
    if((flags & 0x2) != 0)
    {
        g_R.pendingClearZ = z;
        g_R.pendingClearStencil = stencil;
        g_R.hasPendingClearZ = true;
    }
    return true;
}

// Stores d3d8 depth-test state (type: 0 = ZEnable, 1 = ZWriteEnable,
// 2 = ZFUNC with the host D3DCMPFUNC value).
void RendererSetDepthState(unsigned int type, unsigned int value)
{
    RendererLockScope rendererLock;
    if(!g_R.valid)
    {
        return;
    }
    switch(type)
    {
        case 0:
            g_R.depthEnable = value != 0;
            break;
        case 1:
            g_R.depthWrite = value != 0;
            break;
        case 2:
            g_R.depthFunc = value;
            break;
        default:
            break;
    }
}

// Shared recording tail for both draw kinds: validates the vertex layout,
// opens the frame, stages the vertex block, opens the rendering instance,
// and binds pipeline and draw state. Returns true with the staging offset
// of the vertex block in *outOffset.
static bool RecordDrawState(unsigned int primitiveType, unsigned int stride,
                            unsigned int diffuseOffset,
                            unsigned int texCoordOffset, const void* data,
                            VkDeviceSize bytes, VkDeviceSize* outOffset)
{
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

    if(!OpenFrame())
    {
        g_R.valid = false;
        return false;
    }
    VkDeviceSize offset = g_R.vertexCursor;
    if(offset + bytes > g_R.vertexStagingSize)
    {
        // Title-scale vertex blocks (pushbuffer draws) exceed the initial
        // budget: submit the recorded batch first so nothing references the
        // old buffer, then grow. Growing mid-frame is otherwise unsafe — a
        // recorded bind would outlive the destroyed buffer.
        if(!SubmitFrame())
        {
            g_R.valid = false;
            return false;
        }
        constexpr VkDeviceSize kVertexStagingMax = 16 * 1024 * 1024;
        if(bytes > kVertexStagingMax ||
           !EnsureStaging(bytes < g_R.vertexStagingSize * 2
                              ? g_R.vertexStagingSize * 2
                              : bytes))
        {
            printf("VULKAN| renderer: draw exceeds vertex staging (%llu "
                   "bytes); draw dropped\n",
                   static_cast<unsigned long long>(bytes));
            return false;
        }
        offset = g_R.vertexCursor;
        if(offset + bytes > g_R.vertexStagingSize)
        {
            offset = 0;
            g_R.vertexCursor = 0;
        }
    }
    memcpy(static_cast<char*>(g_R.vertexMapped) + offset, data,
           static_cast<size_t>(bytes));
    g_R.vertexCursor = offset + bytes;

    if(!g_R.renderingActive)
    {
        const RTEntry* current =
            g_R.rtCurrent >= 0 ? &g_R.rtTargets[g_R.rtCurrent] : nullptr;
        const unsigned int targetWidth =
            current != nullptr ? current->width : g_R.width;
        const unsigned int targetHeight =
            current != nullptr ? current->height : g_R.height;
        VkRenderingAttachmentInfo attachment = {};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = current != nullptr ? current->view
                                                  : g_R.targetView;
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
        rendering.renderArea.extent = { targetWidth, targetHeight };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        // Every target carries a size-matched depth attachment (cleared to
        // the far plane at creation), so it is always declared.
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = current != nullptr
                                        ? current->depthView
                                        : g_R.mainDepthView;
        depthAttachment.imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = g_R.hasPendingClearZ
                                     ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                     : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil.depth = g_R.pendingClearZ;
        depthAttachment.clearValue.depthStencil.stencil =
            g_R.pendingClearStencil;
        if(g_R.hasPendingClearZ)
        {
            g_R.hasPendingClearZ = false;
        }
        rendering.pDepthAttachment = &depthAttachment;
        BarrierAfterTargetWrite();
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
    // d3d8 depth state: ZEnable / ZWriteEnable / ZFUNC. The d3d8 compare
    // enumeration is VkCompareOp shifted by one (NEVER = 1 vs 0).
    vkCmdSetDepthTestEnable(g_R.commandBuffer,
                            g_R.depthEnable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(g_R.commandBuffer,
                             g_R.depthWrite ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(
        g_R.commandBuffer,
        static_cast<VkCompareOp>(g_R.depthFunc >= 1 && g_R.depthFunc <= 8
                                     ? g_R.depthFunc - 1
                                     : 3 /* LESS_OR_EQUAL */));
    vkCmdPushConstants(g_R.commandBuffer, g_R.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, 16, g_R.viewport);

    // Fresh descriptor set per draw (pool resets after each frame submit):
    // the four stage textures plus the combiner-config UBO slot.
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = g_R.descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &g_R.descriptorLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if(vkAllocateDescriptorSets(g_R.device, &allocateInfo, &set) != VK_SUCCESS)
    {
        printf("VULKAN| descriptor set allocation failed\n");
        return false;
    }
    VkDescriptorImageInfo imageInfos[4] = {};
    VkDescriptorBufferInfo bufferInfo = {};
    VkWriteDescriptorSet writes[5] = {};
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        RendererTexture* texture = g_R.stageTexture[stage];
        imageInfos[stage].sampler = SamplerFor(stage);
        imageInfos[stage].imageView =
            g_R.stageOverrideView[stage] != VK_NULL_HANDLE
                ? g_R.stageOverrideView[stage]
                : (texture != nullptr ? texture->view : g_R.dummyView);
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
    bufferInfo.buffer = g_R.combinerBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = 512; // one combiner-config slot per dynamic bind
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = set;
    writes[4].dstBinding = 1;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[4].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(g_R.device, 5, writes, 0, nullptr);
    const uint32_t combinerDynamicOffset =
        static_cast<uint32_t>(g_R.combinerActiveSlot * 512);
    vkCmdBindDescriptorSets(g_R.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_R.pipelineLayout, 0, 1, &set, 1,
                            &combinerDynamicOffset);

    std::uint32_t stageConstants[20] = {};
    for(unsigned int stage = 0; stage < 4; ++stage)
    {
        stageConstants[stage * 4 + 0] = g_R.stageOp[stage];
        stageConstants[stage * 4 + 1] = g_R.stageArg1[stage];
        stageConstants[stage * 4 + 2] = g_R.stageArg2[stage];
    }
    stageConstants[16] = g_R.useCombiner ? 1u : 0u;
    vkCmdPushConstants(g_R.commandBuffer, g_R.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 16, 80, stageConstants);
    vkCmdBindVertexBuffers(g_R.commandBuffer, 0, 1, &g_R.vertexStaging,
                           &offset);
    *outOffset = offset;
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
    VkDeviceSize offset = 0;
    if(!RecordDrawState(primitiveType, stride, diffuseOffset, texCoordOffset,
                        data, static_cast<VkDeviceSize>(stride) * vertexCount,
                        &offset))
    {
        return false;
    }
    vkCmdDraw(g_R.commandBuffer, vertexCount, 1, 0, 0);
    return true;
}

bool RendererDrawIndexed(unsigned int primitiveType,
                         unsigned int primitiveCount, const void* vertexData,
                         unsigned int vertexCount, unsigned int stride,
                         unsigned int diffuseOffset,
                         unsigned int texCoordOffset, const void* indexData,
                         unsigned int indexCount, int vertexOffset)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || vertexData == nullptr || indexData == nullptr ||
       primitiveCount == 0 || indexCount == 0 || stride == 0)
    {
        return false;
    }
    VkDeviceSize offset = 0;
    if(!RecordDrawState(primitiveType, stride, diffuseOffset, texCoordOffset,
                        vertexData,
                        static_cast<VkDeviceSize>(stride) * vertexCount,
                        &offset))
    {
        return false;
    }
    const VkDeviceSize indexBytes =
        static_cast<VkDeviceSize>(indexCount) * sizeof(std::uint16_t);
    VkDeviceSize indexOffset = g_R.indexCursor;
    if(indexOffset + indexBytes > g_R.indexStagingSize)
    {
        // Mirrors the vertex path: submit first so no recorded bind
        // outlives the replaced buffer, then grow.
        if(!SubmitFrame())
        {
            g_R.valid = false;
            return false;
        }
        constexpr VkDeviceSize kIndexStagingMax = 16 * 1024 * 1024;
        const VkDeviceSize grown =
            indexBytes < g_R.indexStagingSize * 2 ? g_R.indexStagingSize * 2
                                                  : indexBytes;
        if(indexBytes > kIndexStagingMax || !EnsureIndexStaging(grown))
        {
            printf("VULKAN| renderer: draw exceeds index staging (%llu "
                   "bytes); draw dropped\n",
                   static_cast<unsigned long long>(indexBytes));
            return false;
        }
        indexOffset = g_R.indexCursor;
        if(indexOffset + indexBytes > g_R.indexStagingSize)
        {
            indexOffset = 0;
            g_R.indexCursor = 0;
        }
    }
    memcpy(static_cast<char*>(g_R.indexMapped) + indexOffset, indexData,
           static_cast<size_t>(indexBytes));
    g_R.indexCursor = indexOffset + indexBytes;
    vkCmdBindIndexBuffer(g_R.commandBuffer, g_R.indexStaging, indexOffset,
                         VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(g_R.commandBuffer, indexCount, 1, 0,
                     static_cast<std::uint32_t>(vertexOffset), 0);
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
    unsigned int bytesPerPixel =
        TextureFormatBytesPerPixel(hostFormat, &format);
    std::vector<std::uint32_t> decodedStorage;
    const void* uploadPixels = pixels;
    unsigned int uploadPitch = pitch;
    if(bytesPerPixel == 0)
    {
        // Block-compressed uploads decode to RGBA at bind time (the cache
        // key covers the format, so the decode runs once per texture).
        if(!DecodeDxtToBgra(hostFormat, pixels, pitch, width, height,
                            &decodedStorage))
        {
            if(!g_R.textureFormatLogged)
            {
                g_R.textureFormatLogged = true;
                printf("VULKAN| texture host format %u not mirrored yet; "
                       "stage samples white\n",
                       hostFormat);
            }
            g_R.stageTexture[stage] = nullptr;
            return false;
        }
        uploadPixels = decodedStorage.data();
        uploadPitch = width * 4;
        format = VK_FORMAT_B8G8R8A8_UNORM;
        bytesPerPixel = 4;
    }
    if(width > 4096 || height > 4096 ||
       static_cast<VkDeviceSize>(uploadPitch) * height > 16 * 1024 * 1024)
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
               static_cast<const char*>(uploadPixels) +
                   static_cast<size_t>(row) * uploadPitch,
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
    g_R.stageOverrideView[stage] = VK_NULL_HANDLE;
    return true;
}

// Binds a registered render target as a stage texture (render-to-texture
// sampling). Returns false when the key has no target; the caller then
// falls back to the host-texture pull.
bool RendererSetStageRenderTargetTexture(unsigned int stage, void* key)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || stage >= 4 || key == nullptr)
    {
        return false;
    }
    for(unsigned int i = 0; i < g_R.rtTargetCount; ++i)
    {
        if(g_R.rtTargets[i].key == key)
        {
            g_R.stageTexture[stage] = nullptr;
            g_R.stageOverrideView[stage] = g_R.rtTargets[i].view;
            return true;
        }
    }
    return false;
}

void RendererSetRenderTarget(void* key, unsigned int width, unsigned int height)
{
    RendererLockScope rendererLock;
    if(!g_R.valid)
    {
        return;
    }
    if(key == nullptr)
    {
        // Main (backbuffer) target.
        CloseRendering();
        g_R.rtCurrent = -1;
        g_R.viewport[0] = 0.0f;
        g_R.viewport[1] = 0.0f;
        g_R.viewport[2] = static_cast<float>(g_R.width);
        g_R.viewport[3] = static_cast<float>(g_R.height);
        return;
    }
    for(unsigned int i = 0; i < g_R.rtTargetCount; ++i)
    {
        if(g_R.rtTargets[i].key == key)
        {
            CloseRendering();
            g_R.rtCurrent = static_cast<int>(i);
            return;
        }
    }
    if(width == 0 || height == 0 || !CreateRenderableTarget(key, width, height))
    {
        printf("VULKAN| render target %ux%u creation failed; keeping main\n",
               width, height);
        return;
    }
    CloseRendering();
    g_R.rtCurrent = static_cast<int>(g_R.rtTargetCount) - 1;
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

// UBO map (std140 offsets into the CombinConfig block):
//   constants[8] vec4 @ 0, stageA[8] uvec4 @ 128, stageB[8] uvec4 @ 256,
//   meta uvec4 @ 384.
// Config words are host-written into 512-byte ring slots (coherent mapping)
// and each draw binds its slot through a dynamic UBO offset, so every draw
// samples the config that was active when it was recorded (a single shared
// UBO would show the LAST config to every draw in the batch).
void RendererSetPixelShader(const std::uint32_t* def60)
{
    RendererLockScope rendererLock;
    if(!g_R.valid)
    {
        return;
    }
    if(def60 == nullptr)
    {
        g_R.useCombiner = false;
        return;
    }
    std::uint32_t config[104] = {};
    for(unsigned int i = 0; i < 8; ++i)
    {
        config[32 + i * 4 + 0] = def60[34 + i]; // stageA.x = PSRGBInputs
        config[32 + i * 4 + 1] = def60[0 + i];  // stageA.y = PSAlphaInputs
        config[32 + i * 4 + 2] = def60[45 + i]; // stageA.z = PSRGBOutputs
        config[32 + i * 4 + 3] = def60[26 + i]; // stageA.w = PSAlphaOutputs
        config[64 + i * 4 + 0] = def60[10 + i]; // stageB.x = PSConstants0[i]
        config[64 + i * 4 + 1] = def60[18 + i]; // stageB.y = PSConstants1[i]
        config[64 + i * 4 + 2] = def60[57];     // stageB.z = PSC0Mapping
        config[64 + i * 4 + 3] = def60[58];     // stageB.w = PSC1Mapping
    }
    config[96 + 0] = def60[53]; // meta.x = PSCombinerCount (+ flags)
    config[96 + 1] = def60[8];  // meta.y = FinalInputsABCD
    config[96 + 2] = def60[9];  // meta.z = FinalInputsEFG
    config[96 + 3] = def60[59]; // meta.w = FinalCombinerConstants
    config[100 + 0] = def60[43];
    config[100 + 1] = def60[44];

    // Host-written ring slot: coherent mapping makes the config visible to
    // the next submit, and one slot per SetPixelShader keeps every draw's
    // recorded dynamic offset pointing at its own config.
    if(memcmp(g_R.lastConfig, config, sizeof(config)) == 0)
    {
        return; // identical to the active revision; draws keep its slot
    }
    std::uint32_t* slot = static_cast<std::uint32_t*>(g_R.combinerMapped) +
                          g_R.combinerWriteSlot * 128;
    memcpy(slot, config, sizeof(config));
    memcpy(g_R.lastConfig, config, sizeof(config));
    g_R.combinerActiveSlot = g_R.combinerWriteSlot;
    g_R.combinerWriteSlot = (g_R.combinerWriteSlot + 1) % (8192 / 512);
    if(g_R.combinerWriteSlot == 0)
    {
        g_R.ringWraps++;
    }
    g_R.useCombiner = true;
}

void RendererSetPixelShaderConstant(unsigned int registerIndex,
                                    const float* value)
{
    RendererLockScope rendererLock;
    if(!g_R.valid || registerIndex >= 8)
    {
        return;
    }
    std::uint32_t updated[4] = {};
    for(unsigned int c = 0; c < 4; ++c)
    {
        memcpy(&updated[c], value + c, 4);
    }
    if(memcmp(&g_R.lastConfig[registerIndex * 4], updated, 16) == 0)
    {
        return; // identical to the active revision
    }
    std::uint32_t* slot = static_cast<std::uint32_t*>(g_R.combinerMapped) +
                          g_R.combinerWriteSlot * 128;
    memcpy(slot, g_R.lastConfig, 416);
    memcpy(&slot[registerIndex * 4], updated, 16);
    memcpy(g_R.lastConfig, slot, 416);
    g_R.combinerActiveSlot = g_R.combinerWriteSlot;
    g_R.combinerWriteSlot = (g_R.combinerWriteSlot + 1) % (8192 / 512);
    if(g_R.combinerWriteSlot == 0)
    {
        g_R.ringWraps++;
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

namespace
{
// Submits the pending batch, then copies one color target into dst through
// the readback staging (sized for the main target, so a larger render
// target is refused rather than overrun). Caller holds the renderer lock.
bool ReadTargetImage(VkImage image, unsigned int readWidth,
                     unsigned int readHeight, void* dst, unsigned int pitch)
{
    if(!g_R.valid || dst == nullptr || image == VK_NULL_HANDLE)
    {
        return false;
    }
    if(static_cast<VkDeviceSize>(readWidth) * readHeight >
       static_cast<VkDeviceSize>(g_R.width) * g_R.height)
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
    const unsigned int readWidth =
        g_R.rtCurrent >= 0 ? g_R.rtTargets[g_R.rtCurrent].width : g_R.width;
    const unsigned int readHeight =
        g_R.rtCurrent >= 0 ? g_R.rtTargets[g_R.rtCurrent].height : g_R.height;
    region.imageExtent = { readWidth, readHeight, 1 };
    const VkImage readImage =
        g_R.rtCurrent >= 0 ? g_R.rtTargets[g_R.rtCurrent].image : g_R.target;
    vkCmdCopyImageToBuffer(g_R.commandBuffer, readImage,
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
    for(unsigned int row = 0; row < readHeight && row < g_R.height; ++row)
    {
        memcpy(static_cast<char*>(dst) + static_cast<size_t>(row) * pitch,
               static_cast<const char*>(g_R.readbackMapped) +
                   static_cast<size_t>(row) * readWidth * 4,
               readWidth * 4);
    }
    return true;
}
} // namespace

void RendererCurrentTargetSize(unsigned int* width, unsigned int* height)
{
    RendererLockScope rendererLock;
    const bool boundTarget = g_R.rtCurrent >= 0;
    if(width != nullptr)
    {
        *width = boundTarget ? g_R.rtTargets[g_R.rtCurrent].width : g_R.width;
    }
    if(height != nullptr)
    {
        *height =
            boundTarget ? g_R.rtTargets[g_R.rtCurrent].height : g_R.height;
    }
}

bool RendererReadTarget(void* dst, unsigned int pitch)
{
    RendererLockScope rendererLock;
    if(g_R.rtCurrent >= 0)
    {
        const auto& target = g_R.rtTargets[g_R.rtCurrent];
        return ReadTargetImage(target.image, target.width, target.height, dst,
                               pitch);
    }
    return ReadTargetImage(g_R.target, g_R.width, g_R.height, dst, pitch);
}

bool RendererReadMainTarget(void* dst, unsigned int pitch)
{
    RendererLockScope rendererLock;
    return ReadTargetImage(g_R.target, g_R.width, g_R.height, dst, pitch);
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
