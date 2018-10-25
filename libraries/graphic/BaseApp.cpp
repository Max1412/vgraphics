#include "BaseApp.h"
#include "tiny/tiny_obj_loader.h"

namespace vg
{


    BufferInfo BaseApp::createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags& usage, const VmaMemoryUsage properties,
        vk::SharingMode sharingMode, VmaAllocationCreateFlags flags) const
    {
        BufferInfo bufferInfo;

        vk::BufferCreateInfo bufferCreateInfo({}, size, usage, sharingMode);
        if (sharingMode == vk::SharingMode::eConcurrent)
        {
            vg::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
            uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.transferFamily.value() };

            bufferCreateInfo.queueFamilyIndexCount = 2;
            bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = properties;
        allocInfo.flags = flags;
        const auto result = vmaCreateBuffer(m_context.getAllocator(),
            reinterpret_cast<VkBufferCreateInfo*>(&bufferCreateInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&bufferInfo.m_Buffer), &bufferInfo.m_BufferAllocation, &bufferInfo.m_BufferAllocInfo);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Buffer creation failed");

        return bufferInfo;
    }

    void BaseApp::copyBuffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size) const
    {
        vk::CommandBufferAllocateInfo allocInfo(m_transferCommandPool, vk::CommandBufferLevel::ePrimary, 1);

        auto commandBuffer = beginSingleTimeCommands(m_transferCommandPool);

        vk::BufferCopy copyRegion(0, 0, size);
        commandBuffer.copyBuffer(src, dst, copyRegion);

        endSingleTimeCommands(commandBuffer, m_context.getTransferQueue(), m_transferCommandPool);
    }

    vk::CommandBuffer BaseApp::beginSingleTimeCommands(vk::CommandPool commandPool) const
    {
        vk::CommandBufferAllocateInfo allocInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
        auto cmdBuffer = m_context.getDevice().allocateCommandBuffers(allocInfo).at(0);

        vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuffer.begin(beginInfo);

        return cmdBuffer;
    }

    void BaseApp::endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool commandPool) const
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo({}, nullptr, nullptr, 1, &commandBuffer);

        queue.submit(submitInfo, nullptr);
        queue.waitIdle();

        m_context.getDevice().freeCommandBuffers(commandPool, commandBuffer);
    }

    void BaseApp::createFramebuffers()
    {
        m_swapChainFramebuffers.resize(m_context.getSwapChainImageViews().size());
        for (size_t i = 0; i < m_context.getSwapChainImageViews().size(); i++)
        {
            std::array<vk::ImageView, 2> attachments = { m_context.getSwapChainImageViews().at(i), m_depthImageView };

            vk::FramebufferCreateInfo framebufferInfo({}, m_renderpass,
                static_cast<uint32_t>(attachments.size()), attachments.data(),
                m_context.getSwapChainExtent().width,
                m_context.getSwapChainExtent().height,
                1);

            m_swapChainFramebuffers.at(i) = m_context.getDevice().createFramebuffer(framebufferInfo);
        }
    }

    void BaseApp::createCommandPools()
    {
        auto queueFamilyIndices = m_context.findQueueFamilies(m_context.getPhysicalDevice());

        // pool for graphics
        vk::CommandPoolCreateInfo graphicsPoolInfo({}, queueFamilyIndices.graphicsFamily.value());
        m_commandPool = m_context.getDevice().createCommandPool(graphicsPoolInfo);

        // pool for transfer
        vk::CommandPoolCreateInfo transferPoolInfo({}, queueFamilyIndices.transferFamily.value());
        m_transferCommandPool = m_context.getDevice().createCommandPool(transferPoolInfo);

        // pool for compute
        vk::CommandPoolCreateInfo computePoolInfo({}, queueFamilyIndices.computeFamily.value());
        m_computeCommandPool = m_context.getDevice().createCommandPool(transferPoolInfo);
    }

    void BaseApp::drawFrame()
    {
        // wait for the last frame to be finished
        m_context.getDevice().waitForFences(m_inFlightFences.at(m_currentFrame), VK_TRUE, std::numeric_limits<uint64_t>::max());

        auto nextImageResult = m_context.getDevice().acquireNextImageKHR(m_context.getSwapChain(), std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphores.at(m_currentFrame), nullptr);
        uint32_t imageIndex = nextImageResult.value;

        // maybe change this to try/catch as shown below
        if (nextImageResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapChain();
            return;
        }
        else if (nextImageResult.result != vk::Result::eSuccess && nextImageResult.result != vk::Result::eSuboptimalKHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        vk::Semaphore waitSemaphores[] = { m_imageAvailableSemaphores.at(m_currentFrame) };
        vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        vk::Semaphore signalSemaphores[] = { m_renderFinishedSemaphores.at(m_currentFrame) };

        // todo maybe make this more generic e.g. "update per-frame information"
        updatePerFrameInformation(imageIndex);

        vk::SubmitInfo submitInfo(1, waitSemaphores, waitStages, 1, &m_commandBuffers.at(imageIndex), 1, signalSemaphores);

        m_context.getDevice().resetFences(m_inFlightFences.at(m_currentFrame));
        m_context.getGraphicsQueue().submit(submitInfo, m_inFlightFences.at(m_currentFrame));

        std::array<vk::SwapchainKHR, 1> swapChains = { m_context.getSwapChain() };

        vk::PresentInfoKHR presentInfo(1, signalSemaphores, static_cast<uint32_t>(swapChains.size()), swapChains.data(), &imageIndex, nullptr);

        vk::Result presentResult = vk::Result::eSuccess;

        try
        {
            presentResult = m_context.getPresentQueue().presentKHR(presentInfo);
        }
        catch (const vk::OutOfDateKHRError&)
        {
            m_context.setFrameBufferResized(false);
            recreateSwapChain();
        }

        if (presentResult == vk::Result::eSuboptimalKHR || m_context.getFrameBufferResized())
        {
            m_context.setFrameBufferResized(false);
            recreateSwapChain();
        }

        //if (result != vk::Result::eSuccess)
        //    throw std::runtime_error("Failed to present");

        m_context.getPresentQueue().waitIdle();

        m_currentFrame = (m_currentFrame + 1) % m_context.max_frames_in_flight;
    }

    ImageInfo BaseApp::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usageFlags,
        const VmaMemoryUsage properties, vk::SharingMode sharingMode, VmaAllocationCreateFlags flags) const
    {
        ImageInfo returnInfo;

        vk::ImageCreateInfo createInfo({}, vk::ImageType::e2D, format, { width, height, 1 }, mipLevels, 1, vk::SampleCountFlagBits::e1, tiling, usageFlags, sharingMode);
        if (sharingMode == vk::SharingMode::eConcurrent)
        {
            vg::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
            uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.transferFamily.value() };

            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = properties;
        allocInfo.flags = flags;
        vmaCreateImage(m_context.getAllocator(),
            reinterpret_cast<VkImageCreateInfo*>(&createInfo), &allocInfo,
            reinterpret_cast<VkImage*>(&returnInfo.m_Image), &returnInfo.m_ImageAllocation, &returnInfo.m_ImageAllocInfo);

        return returnInfo;
    }

    // todo maybe make graphics/transfer queue selectable somehow. needs to assure the resources are conurrently shared
    void BaseApp::copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height) const
    {
        auto cmdBuffer = beginSingleTimeCommands(m_commandPool);

        vk::ImageSubresourceLayers subreslayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::BufferImageCopy region(0, 0, 0, subreslayers, { 0, 0, 0 }, { width, height, 1 });

        cmdBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

        endSingleTimeCommands(cmdBuffer, m_context.getGraphicsQueue(), m_commandPool);
    }

    void BaseApp::createSyncObjects()
    {
        m_imageAvailableSemaphores.resize(m_context.max_frames_in_flight);
        m_renderFinishedSemaphores.resize(m_context.max_frames_in_flight);
        m_inFlightFences.resize(m_context.max_frames_in_flight);

        vk::SemaphoreCreateInfo semaInfo;
        vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled);

        for (int i = 0; i < m_context.max_frames_in_flight; i++)
        {
            m_imageAvailableSemaphores.at(i) = m_context.getDevice().createSemaphore(semaInfo);
            m_renderFinishedSemaphores.at(i) = m_context.getDevice().createSemaphore(semaInfo);
            m_inFlightFences.at(i) = m_context.getDevice().createFence(fenceInfo);
        }

    }

    void BaseApp::createDepthResources()
    {
        // skipping "findSupportedFormat"
        vk::Format depthFormat = vk::Format::eD32SfloatS8Uint;
        m_depthImage = createImage(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height, 1,
            depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, VMA_MEMORY_USAGE_GPU_ONLY);

        vk::ImageViewCreateInfo viewInfo({}, m_depthImage.m_Image, vk::ImageViewType::e2D, depthFormat, {}, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });
        m_depthImageView = m_context.getDevice().createImageView(viewInfo);

        transitionImageLayout(m_depthImage.m_Image, depthFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    void BaseApp::transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels) const
    {
        auto cmdBuffer = beginSingleTimeCommands(m_commandPool);

        using il = vk::ImageLayout;
        using af = vk::AccessFlagBits;
        using ps = vk::PipelineStageFlagBits;

        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(oldLayout);
        barrier.setNewLayout(newLayout);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(image);
        vk::ImageSubresourceRange subresrange(vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1);
        barrier.setSubresourceRange(subresrange);

        if (newLayout == il::eDepthStencilAttachmentOptimal)
        {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

            if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD16UnormS8Uint)
            {
                barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == il::eUndefined && newLayout == il::eTransferDstOptimal)
        {
            barrier.srcAccessMask = static_cast<af>(0);
            barrier.dstAccessMask = af::eTransferWrite;

            sourceStage = ps::eTopOfPipe;
            destinationStage = ps::eTransfer;
        }
        else if (oldLayout == il::eTransferDstOptimal && newLayout == il::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = af::eTransferWrite;
            barrier.dstAccessMask = af::eShaderRead;

            sourceStage = ps::eTransfer;
            destinationStage = ps::eFragmentShader;
        }
        else if (oldLayout == il::eUndefined && newLayout == il::eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = static_cast<af>(0);
            barrier.dstAccessMask = af::eDepthStencilAttachmentRead | af::eDepthStencilAttachmentWrite;

            sourceStage = ps::eTopOfPipe;
            destinationStage = ps::eEarlyFragmentTests;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        cmdBuffer.pipelineBarrier(sourceStage, destinationStage, static_cast<vk::DependencyFlagBits>(0), 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(cmdBuffer, m_context.getGraphicsQueue(), m_commandPool);
    }

    void BaseApp::generateMipmaps(vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const
    {
        // todo: (left out) check format to be linearly filterable on the current device
        auto cmdBuf = beginSingleTimeCommands(m_commandPool);

        vk::ImageMemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        for (uint32_t i = 1; i < mipLevels; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{0, 0, 0}, { mipWidth, mipHeight, 1 } };
            std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{0, 0, 0}, { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 } };

            vk::ImageBlit blit({ vk::ImageAspectFlagBits::eColor, i - 1, 0, 1 }, srcOffsets, { vk::ImageAspectFlagBits::eColor, i, 0, 1 }, dstOffsets);

            cmdBuf.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);
    }
}

