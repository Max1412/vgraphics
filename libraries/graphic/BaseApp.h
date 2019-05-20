#pragma once
#include "vma/vk_mem_alloc.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "Context.h"


namespace vg
{

    struct ASInfo
    {
        vk::AccelerationStructureNV m_AS = nullptr;
        VmaAllocation m_BufferAllocation = nullptr;
        VmaAllocationInfo m_BufferAllocInfo = {};
    };

    struct BufferInfo
    {
        vk::Buffer m_Buffer = nullptr;
        VmaAllocation m_BufferAllocation = nullptr;
        VmaAllocationInfo m_BufferAllocInfo = {};
    };

    struct ImageInfo
    {
        vk::Image m_Image = nullptr;
        VmaAllocation m_ImageAllocation = nullptr;
        VmaAllocationInfo m_ImageAllocInfo = {};
        unsigned mipLevels;
    };

    struct UniformBufferObject
    {
        glm::mat4 model;
        //glm::mat4 view;
        //glm::mat4 proj;
    };

    class BaseApp
    {
    public:
		BaseApp(const std::vector<const char*>& requiredDeviceExtensions);
        virtual void recreateSwapChain() = 0;

        // todo maybe make this more generic e.g. "update per-frame information"
        //virtual void updatePerFrameInformation(uint32_t) = 0;
        //virtual void createPerFrameInformation() = 0;
        virtual void recordPerFrameCommandBuffers(uint32_t currentImage) = 0;

        virtual void buildImguiCmdBufferAndSubmit(const uint32_t imageIndex)
        {
            // submit to queue without any commands to signal the semaphore and fence to end the frame
            vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
            vk::SubmitInfo submitInfo(1, &m_graphicsRenderFinishedSemaphores.at(m_currentFrame), waitStages, 0, nullptr, 1, &m_guiFinishedSemaphores.at(m_currentFrame));

            m_context.getDevice().resetFences(m_inFlightFences.at(m_currentFrame));
            m_context.getGraphicsQueue().submit(1, &submitInfo, m_inFlightFences.at(m_currentFrame));
        };
        void allocBufferVma(BufferInfo& in, vk::BufferCreateInfo bufferCreateInfo, const VmaMemoryUsage properties, const VmaAllocationCreateFlags flags = 0) const;
        BufferInfo createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags& usage, const VmaMemoryUsage properties,
            vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0) const;

        void copyBuffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size) const;

        template <class T>
        BufferInfo fillBufferTroughStagedTransferForComputeQueue(const std::vector<T>& data, vk::BufferUsageFlags actualBufferUsage) const;

        template <typename T>
        BufferInfo fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage) const;

        vk::CommandBuffer beginSingleTimeCommands(vk::CommandPool commandPool) const;

        void endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool commandPool, const SemaphoreInfos& si = {}) const;

        void createSwapchainFramebuffers(const vk::RenderPass& renderpass);

        void createCommandPools();

        void drawFrame();

        ImageInfo createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usageFlags,
            const VmaMemoryUsage properties, vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0, uint32_t layers = 1) const;

        void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height) const;

        ImageInfo createTextureImageFromLoaded(const ImageLoadInfo & ili) const;

        ImageInfo createTextureImage(const char* name) const;

        void createSyncObjects();

        void createDepthResources();

        void transitionInCmdBuf(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels, vk::CommandBuffer cmdBuffer) const;

        void transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels = 1, const SemaphoreInfos& si = {}) const;

        void generateMipmaps(vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;

        void setupImgui();

        void createQueryPool(const uint32_t queryCount = 1, const vk::QueryType queryType = vk::QueryType::eTimestamp);

    protected:

        Context m_context;

        std::vector<vk::Semaphore> m_imageAvailableSemaphores;
        std::vector<vk::Semaphore> m_graphicsRenderFinishedSemaphores;
        std::vector<vk::Semaphore> m_guiFinishedSemaphores;
        std::vector<vk::Semaphore> m_ASupdateSemaphores;

        bool m_useAsync = false;

        std::vector<vk::Fence> m_inFlightFences;
        int m_currentFrame = 0;

        std::vector<vk::CommandBuffer> m_commandBuffers;
        std::vector<vk::CommandBuffer> m_staticSecondaryCommandBuffers;
        std::vector<vk::CommandBuffer> m_perFrameSecondaryCommandBuffers;
        std::vector<vk::CommandBuffer> m_imguiCommandBuffers;


        std::vector<vk::Framebuffer> m_swapChainFramebuffers;


        ImageInfo m_depthImage;
        vk::ImageView m_depthImageView;

        vk::CommandPool m_commandPool;
        vk::CommandPool m_transferCommandPool;
        vk::CommandPool m_computeCommandPool;

        vk::QueryPool m_queryPool;
        
    };

    template <typename T>
    BufferInfo BaseApp::fillBufferTroughStagedTransferForComputeQueue(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage) const
    {
        vk::DeviceSize bufferSize = sizeof(T) * data.size();

        auto stagingBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eConcurrent, VMA_ALLOCATION_CREATE_MAPPED_BIT);

        // staging buffer is persistently mapped, no mapping necessary
        memcpy(stagingBufferInfo.m_BufferAllocInfo.pMappedData, data.data(), stagingBufferInfo.m_BufferAllocInfo.size); // TODO maybe using this size is wrong

        vg::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
        std::array queueFamilyIndices = { indices.graphicsFamily.value(), indices.computeFamily.value(), indices.transferFamily.value() };
        vk::BufferCreateInfo bufferCreateInfo({}, bufferSize, vk::BufferUsageFlagBits::eTransferDst | actualBufferUsage, vk::SharingMode::eConcurrent, static_cast<uint32_t>(queueFamilyIndices.size()), queueFamilyIndices.data());
        BufferInfo returnBufferInfo;
        allocBufferVma(returnBufferInfo, bufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);

        copyBuffer(stagingBufferInfo.m_Buffer, returnBufferInfo.m_Buffer, bufferSize);

        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(stagingBufferInfo.m_Buffer), stagingBufferInfo.m_BufferAllocation);

        return returnBufferInfo;
    }

    template <typename T>
    BufferInfo BaseApp::fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage) const
    {
        vk::DeviceSize bufferSize = sizeof(T) * data.size();

        auto stagingBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eConcurrent, VMA_ALLOCATION_CREATE_MAPPED_BIT);

        // staging buffer is persistently mapped, no mapping necessary
        memcpy(stagingBufferInfo.m_BufferAllocInfo.pMappedData, data.data(), stagingBufferInfo.m_BufferAllocInfo.size);

        auto returnBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | actualBufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, vk::SharingMode::eConcurrent);

        copyBuffer(stagingBufferInfo.m_Buffer, returnBufferInfo.m_Buffer, bufferSize);

        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(stagingBufferInfo.m_Buffer), stagingBufferInfo.m_BufferAllocation);

        return returnBufferInfo;
    }

}
