#pragma once
#include "vma/vk_mem_alloc.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "Context.h"


namespace vg
{

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
        virtual void recreateSwapChain() = 0;

        // todo maybe make this more generic e.g. "update per-frame information"
        virtual void updatePerFrameInformation(uint32_t) = 0;
        virtual void createPerFrameInformation() = 0;

        BufferInfo createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags& usage, const VmaMemoryUsage properties,
            vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0) const;

        void copyBuffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size) const;

        template <typename T>
        BufferInfo fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage) const;

        vk::CommandBuffer beginSingleTimeCommands(vk::CommandPool commandPool) const;

        void endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool commandPool) const;

        void createFramebuffers();

        void createCommandPools();

        void drawFrame();

        ImageInfo createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usageFlags,
            const VmaMemoryUsage properties, vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0) const;

        void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height) const;

        void createSyncObjects();

        void createDepthResources();

        void transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels = 1) const;

        void generateMipmaps(vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;



    protected:

        Context m_context;

        vk::RenderPass m_renderpass;

        std::vector<vk::Semaphore> m_imageAvailableSemaphores;
        std::vector<vk::Semaphore> m_renderFinishedSemaphores;
        std::vector<vk::Fence> m_inFlightFences;
        int m_currentFrame = 0;

        std::vector<vk::CommandBuffer> m_commandBuffers;


        std::vector<vk::Framebuffer> m_swapChainFramebuffers;


        ImageInfo m_depthImage;
        vk::ImageView m_depthImageView;

        vk::CommandPool m_commandPool;
        vk::CommandPool m_transferCommandPool;
        vk::CommandPool m_computeCommandPool;


    };

    template <typename T>
    BufferInfo BaseApp::fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage) const
    {
        vk::DeviceSize bufferSize = sizeof(T) * data.size();

        auto stagingBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eConcurrent, VMA_ALLOCATION_CREATE_MAPPED_BIT);

        // staging buffer is persistently mapped, no mapping necessary
        memcpy(stagingBufferInfo.m_BufferAllocInfo.pMappedData, data.data(), stagingBufferInfo.m_BufferAllocInfo.size); // TODO maybe using this size is wrong

        auto returnBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | actualBufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, vk::SharingMode::eConcurrent);

        copyBuffer(stagingBufferInfo.m_Buffer, returnBufferInfo.m_Buffer, bufferSize);

        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(stagingBufferInfo.m_Buffer), stagingBufferInfo.m_BufferAllocation);

        return returnBufferInfo;
    }

}
