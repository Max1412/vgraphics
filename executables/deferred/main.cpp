#include <iostream>
#include <filesystem>

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#include "graphic/Context.h"
#include "graphic/BaseApp.h"
#include "graphic/Definitions.h"
#include "userinput/Pilotview.h"
#include "geometry/scene.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_glfw.h"
#include "utility/Timer.h"
#include "stb/stb_image.h"

namespace vg
{

    class DeferredApp : public BaseApp
    {
    public:
        DeferredApp() :
    		BaseApp({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters" }),
    		m_camera(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height),
			m_scene("Sponza/sponza.obj")
		{
            //createRenderPass();

            createCommandPools();
            createSceneInformation("Sponza/");

            createDepthResources();

            createGBufferRenderpass();
            createFullscreenLightingRenderpass();

            createSwapchainFramebuffers(m_fullscreenLightingRenderpass);
            createGBufferResources();

            createVertexBuffer();
            createIndexBuffer();
            createIndirectDrawBuffer();
            createPerGeometryBuffers();

            createGBufferDescriptors();
            createFullscreenLightingDescriptors();

            createGBufferPipeline();
            createFullscreenLightingPipeline();


            createPerFrameInformation();

            createAllCommandBuffers();
            createSyncObjects();

            createQueryPool();

            setupImgui();
        }

        // todo clarify what is here and what is in cleanupswapchain
        ~DeferredApp()
        {
            m_context.getDevice().destroyQueryPool(m_queryPool);

            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indexBufferInfo.m_Buffer), m_indexBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_vertexBufferInfo.m_Buffer), m_vertexBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indirectDrawBufferInfo.m_Buffer), m_indirectDrawBufferInfo.m_BufferAllocation);

            m_context.getDevice().destroyImageView(m_depthImageView);
            vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);

            for (int i = 0; i < m_context.max_frames_in_flight; i++)
            {
                m_context.getDevice().destroySemaphore(m_imageAvailableSemaphores.at(i));
                m_context.getDevice().destroySemaphore(m_graphicsRenderFinishedSemaphores.at(i));
                m_context.getDevice().destroySemaphore(m_guiFinishedSemaphores.at(i));
                m_context.getDevice().destroyFence(m_inFlightFences.at(i));
            }

            m_context.getDevice().destroyCommandPool(m_commandPool);
            m_context.getDevice().destroyCommandPool(m_transferCommandPool);
            m_context.getDevice().destroyCommandPool(m_computeCommandPool);

            for (const auto framebuffer : m_swapChainFramebuffers)
                m_context.getDevice().destroyFramebuffer(framebuffer);

            for (const auto& framebuffer : m_gbufferFramebuffers)
                m_context.getDevice().destroyFramebuffer(framebuffer);

            for(const auto& sampler : m_allImageSamplers)
                m_context.getDevice().destroySampler(sampler);
            for (const auto& view : m_allImageViews)
                m_context.getDevice().destroyImageView(view);
            for(const auto& image : m_allImages)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);

            for(const auto& sampler : m_gbufferPositionSamplers)
                m_context.getDevice().destroySampler(sampler);
            for (const auto& sampler : m_gbufferNormalSamplers)
                m_context.getDevice().destroySampler(sampler);
            for (const auto& sampler : m_gbufferUVSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& view : m_gbufferPositionImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& view : m_gbufferNormalImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& view : m_gbufferUVImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& view : m_gbufferDepthImageViews)
                m_context.getDevice().destroyImageView(view);

            for (const auto& image : m_gbufferPositionImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& image : m_gbufferNormalImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& image : m_gbufferUVImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& image : m_gbufferDepthImages)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);

            m_context.getDevice().destroyDescriptorPool(m_gbufferDescriptorPool);
            m_context.getDevice().destroyDescriptorPool(m_fullScreenLightingDescriptorPool);

            m_context.getDevice().destroyDescriptorSetLayout(m_gbufferDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_fullScreenLightingDescriptorSetLayout);

            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_modelMatrixBufferInfo.m_Buffer), m_modelMatrixBufferInfo.m_BufferAllocation);

            m_context.getDevice().destroyPipeline(m_gbufferGraphicsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_gbufferPipelineLayout);
            m_context.getDevice().destroyRenderPass(m_gbufferRenderpass);

            m_context.getDevice().destroyPipeline(m_fullscreenLightingPipeline);
            m_context.getDevice().destroyPipelineLayout(m_fullscreenLightingPipelineLayout);
            m_context.getDevice().destroyRenderPass(m_fullscreenLightingRenderpass);
            // cleanup here
        }

        void createGBufferRenderpass()
        {
            // positions, normals as color attachments

            vk::AttachmentDescription positionAttachment({}, vk::Format::eR32G32B32A32Sfloat, //TODO use saved format, not hard-coded
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal //TODO is this the right layout? maybe colorattachmentoptimal
            );

            vk::AttachmentReference positionAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

            vk::AttachmentDescription normalAttachment({}, vk::Format::eR32G32B32A32Sfloat, //TODO use saved format, not hard-coded
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal //TODO is this the right layout? maybe colorattachmentoptimal
            );

            vk::AttachmentReference normalAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal);

            vk::AttachmentDescription uvAttachment({}, vk::Format::eR32G32B32A32Sfloat, //TODO use saved format, not hard-coded
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal //TODO is this the right layout? maybe colorattachmentoptimal
            );

            vk::AttachmentReference uvAttachmentRef(2, vk::ImageLayout::eColorAttachmentOptimal);


            vk::AttachmentDescription depthAttachment({}, vk::Format::eD32SfloatS8Uint,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal
            );

            vk::AttachmentReference depthAttachmentRef(3, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion);

            std::array<vk::AttachmentReference, 3> colorAttachmentRefs = { positionAttachmentRef, normalAttachmentRef, uvAttachmentRef };

            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics,
                0, nullptr,                 // input attachments (standard values)
                static_cast<uint32_t>(colorAttachmentRefs.size()), colorAttachmentRefs.data(),     // color attachments: layout (location = 0) out -> colorAttachmentRef is at index 0
                nullptr,                    // no resolve attachment
                &depthAttachmentRef);       // depth stencil attachment
                                            // other attachment at standard values: Preserved

            std::array<vk::AttachmentDescription, 4> attachments = { positionAttachment, normalAttachment, uvAttachment, depthAttachment };

            vk::RenderPassCreateInfo renderpassInfo({}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1, &subpass, 1, &dependency);

            m_gbufferRenderpass = m_context.getDevice().createRenderPass(renderpassInfo);

        }



        void createGBufferDescriptors()
        {
            // fragment shader outputs are handled by framebuffers, so only 1 DS is needed even though multi-buffered output textures are used

            // 1: create descriptor set layout

            // inputs //TODO maybe put model-matrix in per-mesh buffer
            vk::DescriptorSetLayoutBinding modelMatrixSSBOLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr);
            vk::DescriptorSetLayoutBinding perMeshInformationIndirectDrawSSBOLB(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImages.size()), vk::ShaderStageFlagBits::eFragment, nullptr);

            std::array<vk::DescriptorSetLayoutBinding, 3> bindings = { modelMatrixSSBOLayoutBinding, perMeshInformationIndirectDrawSSBOLB, allTexturesLayoutBinding };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_gbufferDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            // 2: create descriptor pool
            vk::DescriptorPoolSize perMeshInformationIndirectDrawSSBO(vk::DescriptorType::eStorageBuffer, 1);
            vk::DescriptorPoolSize poolSizemodelMatrixSSBO(vk::DescriptorType::eStorageBuffer, 1);
            vk::DescriptorPoolSize poolSizeAllImages(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImageSamplers.size()));

            std::array<vk::DescriptorPoolSize, 3> poolSizes = { perMeshInformationIndirectDrawSSBO, poolSizemodelMatrixSSBO, poolSizeAllImages };

            vk::DescriptorPoolCreateInfo poolInfo({}, 1, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            m_gbufferDescriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);

            // 3: create descriptor set

            vk::DescriptorSetAllocateInfo allocInfo(m_gbufferDescriptorPool, 1, &m_gbufferDescriptorSetLayout);
            m_gbufferDescriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

            // model matrix buffer
            vk::DescriptorBufferInfo bufferInfo(m_modelMatrixBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWrite(m_gbufferDescriptorSets.at(0), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo, nullptr);
            vk::DescriptorBufferInfo perMeshInformationIndirectDrawSSBOInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWritePerMeshInfo(m_gbufferDescriptorSets.at(0), 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &perMeshInformationIndirectDrawSSBOInfo, nullptr);

            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for (int i = 0; i < m_allImages.size(); i++)
            {
                allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::WriteDescriptorSet descWriteAllImages(m_gbufferDescriptorSets.at(0), 1, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

            std::array<vk::WriteDescriptorSet, 3> descriptorWrites = { descWrite, descWriteAllImages, descWritePerMeshInfo };
            m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }

        void createGBufferPipeline()
        {
            const auto vertShaderCode = Utility::readFile("deferred/gbuffer.vert.spv");
            const auto fragShaderCode = Utility::readFile("deferred/gbuffer.frag.spv");

            const auto vertShaderModule = m_context.createShaderModule(vertShaderCode);
            const auto fragShaderModule = m_context.createShaderModule(fragShaderCode);

            // specialization constant for the number of textures
            vk::SpecializationMapEntry mapEntry(0, 0, sizeof(int32_t));
            int32_t numTextures = static_cast<int32_t>(m_allImages.size());
            vk::SpecializationInfo numTexturesSpecInfo(1, &mapEntry, sizeof(int32_t), &numTextures);

            const vk::PipelineShaderStageCreateInfo vertShaderStageInfo({}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
            const vk::PipelineShaderStageCreateInfo fragShaderStageInfo({}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main", &numTexturesSpecInfo);

            const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

            auto bindingDescription = vg::VertexPosUvNormal::getBindingDescription();
            auto attributeDescriptions = vg::VertexPosUvNormal::getAttributeDescriptions();
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 1, &bindingDescription,
                static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, false);

            vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_context.getWidth()), static_cast<float>(m_context.getHeight()), 0.0f, 1.0f);

            vk::Rect2D scissor({ 0, 0 }, m_context.getSwapChainExtent());

            vk::PipelineViewportStateCreateInfo viewportState({}, 1, &viewport, 1, &scissor);

            vk::PipelineRasterizationStateCreateInfo rasterizer({}, false, false,
                vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise,
                false, 0, 0, 0, 1.0f);

            vk::PipelineMultisampleStateCreateInfo multisampling({}, vk::SampleCountFlagBits::e1, false);

            vk::PipelineDepthStencilStateCreateInfo depthStencil({}, true, true, vk::CompareOp::eLess);

            // no blending needed
            vk::PipelineColorBlendAttachmentState colorBlendAttachment(false); // standard values for blending.
            colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            //vk::PipelineColorBlendAttachmentState uvBlendAttachment(false); // if blending is ON, this is needed
            //uvBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG);
            // we need 2 blend attachments for 2 framebuffer attachments
            std::array<vk::PipelineColorBlendAttachmentState, 3> blendAttachments = { colorBlendAttachment, colorBlendAttachment, colorBlendAttachment };
            // standard values for now
            vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy,
                static_cast<uint32_t>(blendAttachments.size()), blendAttachments.data(),
                std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

            // no dynamic state needed
            //std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
            //vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates.size(), dynamicStates.data());

            // push view & proj matrix
            std::array<vk::PushConstantRange, 1> vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, 2 * sizeof(glm::mat4)},
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 1, &m_gbufferDescriptorSetLayout, static_cast<uint32_t>(vpcr.size()), vpcr.data());

            m_gbufferPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);

            vk::GraphicsPipelineCreateInfo pipelineInfo({}, 2, shaderStages, &vertexInputInfo, &inputAssembly, nullptr,
                &viewportState, &rasterizer, &multisampling, &depthStencil, &colorBlending, nullptr,
                m_gbufferPipelineLayout, m_gbufferRenderpass, 0);


            m_gbufferGraphicsPipeline = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);

            m_context.getDevice().destroyShaderModule(vertShaderModule);
            m_context.getDevice().destroyShaderModule(fragShaderModule);
        }
        

        void createGBufferResources()
        {
            // create images
            const auto ext = m_context.getSwapChainExtent();
            for(size_t i = 0; i < m_context.getSwapChainImages().size(); i++)
            {
                // image
                m_gbufferPositionImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32G32B32A32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY)
                );

                m_gbufferNormalImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32G32B32A32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY)
                );

                m_gbufferUVImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32G32B32A32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY)
                );

                // view //TODO maybe only one view is needed because they're all the same?
                const vk::ImageViewCreateInfo posViewInfo({},
                    m_gbufferPositionImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32G32B32A32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, m_gbufferPositionImageInfos.at(i).mipLevels, 0, 1 });
                m_gbufferPositionImageViews.push_back(m_context.getDevice().createImageView(posViewInfo));

                const vk::ImageViewCreateInfo normalViewInfo({},
                    m_gbufferNormalImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32G32B32A32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, m_gbufferNormalImageInfos.at(i).mipLevels, 0, 1 });
                m_gbufferNormalImageViews.push_back(m_context.getDevice().createImageView(normalViewInfo));

                const vk::ImageViewCreateInfo uvViewInfo({},
                    m_gbufferUVImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32G32B32A32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, m_gbufferUVImageInfos.at(i).mipLevels, 0, 1 });
                m_gbufferUVImageViews.push_back(m_context.getDevice().createImageView(uvViewInfo));

                // sampler
                vk::SamplerCreateInfo samplerPosInfo({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, true, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_gbufferPositionImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_gbufferPositionSamplers.push_back(m_context.getDevice().createSampler(samplerPosInfo));

                vk::SamplerCreateInfo samplerNormalInfo({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, true, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_gbufferNormalImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_gbufferNormalSamplers.push_back(m_context.getDevice().createSampler(samplerNormalInfo));

                vk::SamplerCreateInfo samplerUVInfo({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, true, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_gbufferUVImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_gbufferUVSamplers.push_back(m_context.getDevice().createSampler(samplerUVInfo));
            }

            // create depth images //TODO maybe those aren't even needed
            vk::Format depthFormat = vk::Format::eD32SfloatS8Uint;
            for(size_t i = 0; i < m_context.getSwapChainImages().size(); i++)
            {
                m_gbufferDepthImages.push_back(createImage(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height, 1,
                    depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, VMA_MEMORY_USAGE_GPU_ONLY));

                vk::ImageViewCreateInfo viewInfo({}, m_gbufferDepthImages.at(i).m_Image, vk::ImageViewType::e2D, depthFormat, {}, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });
                m_gbufferDepthImageViews.push_back(m_context.getDevice().createImageView(viewInfo));

                transitionImageLayout(m_gbufferDepthImages.at(i).m_Image, depthFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
            }   
                

            // create fbos
            for (size_t i = 0; i < m_context.getSwapChainImages().size(); i++)
            {
                // TODO attach missing attachments (pos, normal, geometry ID, ...)
                std::array<vk::ImageView, 4> attachments = {
                    m_gbufferPositionImageViews.at(i),
                    m_gbufferNormalImageViews.at(i),
                    m_gbufferUVImageViews.at(i),
                    m_gbufferDepthImageViews.at(i) }; //TODO what depth image to use?

                vk::FramebufferCreateInfo framebufferInfo({}, m_gbufferRenderpass,
                    static_cast<uint32_t>(attachments.size()), attachments.data(),
                    ext.width, ext.height, 1);

                m_gbufferFramebuffers.push_back(m_context.getDevice().createFramebuffer(framebufferInfo));
            }
        }

        void createFullscreenLightingRenderpass()
        {
            vk::AttachmentDescription colorAttachment({}, m_context.getSwapChainImageFormat(),
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal //TODO 
            );

            vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

            //TODO is depth attachment even needed?
            vk::AttachmentDescription depthAttachment({}, vk::Format::eD32SfloatS8Uint,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal //TODO
            );

            vk::AttachmentReference depthAttachmentRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion);


            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics,
                0, nullptr,                 // input attachments (standard values)
                1, &colorAttachmentRef,     // color attachments: layout (location = 0) out -> colorAttachmentRef is at index 0
                nullptr,                    // no resolve attachment
                &depthAttachmentRef);       // depth stencil attachment
                                            // other attachment at standard values: Preserved

            std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

            vk::RenderPassCreateInfo renderpassInfo({}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1, &subpass, 1, &dependency);

            m_fullscreenLightingRenderpass = m_context.getDevice().createRenderPass(renderpassInfo);
        }

        void createFullscreenLightingPipeline()
        {

            const auto vertShaderCode = Utility::readFile("deferred/fullscreen.vert.spv");
            const auto fragShaderCode = Utility::readFile("deferred/fullscreenLighting.frag.spv");

            const auto vertShaderModule = m_context.createShaderModule(vertShaderCode);
            const auto fragShaderModule = m_context.createShaderModule(fragShaderCode);

            // specialization constant for the number of textures
            vk::SpecializationMapEntry mapEntry(0, 0, sizeof(int32_t));
            int32_t numTextures = static_cast<int32_t>(m_allImages.size());
            vk::SpecializationInfo numTexturesSpecInfo(1, &mapEntry, sizeof(int32_t), &numTextures);

            const vk::PipelineShaderStageCreateInfo vertShaderStageInfo({}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
            const vk::PipelineShaderStageCreateInfo fragShaderStageInfo({}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main", &numTexturesSpecInfo);

            const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

            // no vertex input
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 0, nullptr, 0, nullptr);

            // everything else is standard

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, false);

            vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_context.getWidth()), static_cast<float>(m_context.getHeight()), 0.0f, 1.0f);

            vk::Rect2D scissor({ 0, 0 }, m_context.getSwapChainExtent());

            vk::PipelineViewportStateCreateInfo viewportState({}, 1, &viewport, 1, &scissor);

            vk::PipelineRasterizationStateCreateInfo rasterizer({}, false, false,
                vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eCounterClockwise,
                false, 0, 0, 0, 1.0f);

            vk::PipelineMultisampleStateCreateInfo multisampling({}, vk::SampleCountFlagBits::e1, false);

            vk::PipelineDepthStencilStateCreateInfo depthStencil({}, true, true, vk::CompareOp::eLess);

            // no blending needed
            vk::PipelineColorBlendAttachmentState colorBlendAttachment(false); // standard values for blending.
            colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

            // standard values for now
            vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment,
                std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});



            // push view & proj matrix
            std::array<vk::PushConstantRange, 1> vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, 2 * sizeof(glm::mat4)},
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 1, &m_fullScreenLightingDescriptorSetLayout, static_cast<uint32_t>(vpcr.size()), vpcr.data());

            m_fullscreenLightingPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);

            vk::GraphicsPipelineCreateInfo pipelineInfo;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = nullptr;// &dynamicState;

            pipelineInfo.layout = m_fullscreenLightingPipelineLayout;
            pipelineInfo.renderPass = m_fullscreenLightingRenderpass;
            pipelineInfo.subpass = 0;   // this is an index
            // missing: pipeline derivation

            m_fullscreenLightingPipeline = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);

            m_context.getDevice().destroyShaderModule(vertShaderModule);
            m_context.getDevice().destroyShaderModule(fragShaderModule);
        }


        void createFullscreenLightingDescriptors()
        {
            // inputs
            vk::DescriptorSetLayoutBinding perMeshInformationIndirectDrawSSBOLB(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImages.size()), vk::ShaderStageFlagBits::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding positionTextureBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding normalTextureBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding uvTextureBinding(4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

            std::array<vk::DescriptorSetLayoutBinding, 5> bindings = {
                perMeshInformationIndirectDrawSSBOLB,
                allTexturesLayoutBinding,
                positionTextureBinding,
                normalTextureBinding,
                uvTextureBinding };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_fullScreenLightingDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            // 2: create descriptor pool

            vk::DescriptorPoolSize perMeshInformationIndirectDrawSSBO(vk::DescriptorType::eStorageBuffer, 1);
            vk::DescriptorPoolSize poolSizeAllImages(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImageSamplers.size()));
            vk::DescriptorPoolSize gbufferImages(vk::DescriptorType::eCombinedImageSampler,3);


            std::array<vk::DescriptorPoolSize, 3> poolSizes = { perMeshInformationIndirectDrawSSBO, poolSizeAllImages, gbufferImages };

            vk::DescriptorPoolCreateInfo poolInfo({}, static_cast<uint32_t>(m_swapChainFramebuffers.size()), static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            m_fullScreenLightingDescriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);

            // create n descriptor sets, 1 for each multi-buffered gbuffer
            std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_fullScreenLightingDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo(m_fullScreenLightingDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
            m_fullScreenLightingDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);


            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for (int i = 0; i < m_allImages.size(); i++)
            {
                allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::DescriptorBufferInfo perMeshInformationIndirectDrawSSBOInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);

            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                //TODO coordinate bindings with shader
                vk::WriteDescriptorSet descWritePerMeshInfo(m_fullScreenLightingDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &perMeshInformationIndirectDrawSSBOInfo, nullptr);
                vk::WriteDescriptorSet descWriteAllImages(m_fullScreenLightingDescriptorSets.at(i), 1, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);
                
                vk::DescriptorImageInfo gbufferPosInfo(m_gbufferPositionSamplers.at(i), m_gbufferPositionImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet descWriteGBufferPos(m_fullScreenLightingDescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &gbufferPosInfo, nullptr, nullptr);

                vk::DescriptorImageInfo gbufferNormalInfo(m_gbufferNormalSamplers.at(i), m_gbufferNormalImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet descWriteGBufferNormal(m_fullScreenLightingDescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &gbufferNormalInfo, nullptr, nullptr);

                vk::DescriptorImageInfo gbufferUVInfo(m_gbufferUVSamplers.at(i), m_gbufferUVImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet descWriteGBufferUV(m_fullScreenLightingDescriptorSets.at(i), 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &gbufferUVInfo, nullptr, nullptr);


                std::array<vk::WriteDescriptorSet, 5> descriptorWrites = { 
                    descWritePerMeshInfo, 
                    descWriteAllImages,
                    descWriteGBufferPos,
                    descWriteGBufferNormal,
                    descWriteGBufferUV };

                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        
        }

        void createSceneInformation(const char * foldername)
        {
            m_context.getLogger()->info("Loading Textures...");
            // load all images
            std::vector<ImageLoadInfo> loadedImages(m_scene.getIndexedDiffuseTexturePaths().size() + m_scene.getIndexedSpecularTexturePaths().size());
            stbi_set_flip_vertically_on_load(true);

#pragma omp parallel for
            for (int i = 0; i < static_cast<int>(m_scene.getIndexedDiffuseTexturePaths().size()); i++)
            {
                auto path = g_resourcesPath;
                const auto name = std::string(std::string(foldername) + m_scene.getIndexedDiffuseTexturePaths().at(i).second);
                path.append(name);
                loadedImages.at(i).pixels = stbi_load(path.string().c_str(), &loadedImages.at(i).texWidth, &loadedImages.at(i).texHeight, &loadedImages.at(i).texChannels, STBI_rgb_alpha);
                loadedImages.at(i).mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(loadedImages.at(i).texWidth, loadedImages.at(i).texHeight)))) + 1;
            }

#pragma omp parallel for
            for (int i = static_cast<int>(m_scene.getIndexedDiffuseTexturePaths().size()); i < static_cast<int>(loadedImages.size()); i++)
            {
                auto path = g_resourcesPath;
                const auto name = std::string(std::string(foldername) + m_scene.getIndexedSpecularTexturePaths().at(i - m_scene.getIndexedDiffuseTexturePaths().size()).second);
                path.append(name);
                loadedImages.at(i).pixels = stbi_load(path.string().c_str(), &loadedImages.at(i).texWidth, &loadedImages.at(i).texHeight, &loadedImages.at(i).texChannels, STBI_rgb_alpha);
                loadedImages.at(i).mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(loadedImages.at(i).texWidth, loadedImages.at(i).texHeight)))) + 1;
            }

            for (const auto& ili : loadedImages)
            {
                // load image, fill resource, create mipmaps
                const auto imageInfo = createTextureImageFromLoaded(ili);
                m_allImages.push_back(imageInfo);

                // create view for image
                vk::ImageViewCreateInfo viewInfo({}, imageInfo.m_Image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, {}, { vk::ImageAspectFlagBits::eColor, 0, imageInfo.mipLevels, 0, 1 });
                m_allImageViews.push_back(m_context.getDevice().createImageView(viewInfo));

                vk::SamplerCreateInfo samplerInfo({},
                    vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, true, 16.0f, false, vk::CompareOp::eAlways, 0.0f, static_cast<float>(imageInfo.mipLevels), vk::BorderColor::eIntOpaqueBlack, false
                );
                m_allImageSamplers.push_back(m_context.getDevice().createSampler(samplerInfo));

            }

            m_context.getLogger()->info("Texture loading complete.");
        }

        void createVertexBuffer()
        {
            m_vertexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getVertices(), vk::BufferUsageFlagBits::eVertexBuffer);
        }

        void createIndexBuffer()
        {
            m_indexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getIndices(), vk::BufferUsageFlagBits::eIndexBuffer);
        }

        void createIndirectDrawBuffer()
        {
            m_indirectDrawBufferInfo = fillBufferTroughStagedTransfer(m_scene.getDrawCommandData(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createPerGeometryBuffers()
        {
            m_modelMatrixBufferInfo = fillBufferTroughStagedTransfer(m_scene.getModelMatrices(), vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createPerFrameInformation()
        {
            m_camera = Pilotview(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height);
            m_camera.setSensitivity(0.01f);
            m_projection = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10000.0f);
            m_projection[1][1] *= -1;

            m_camera.update(m_context.getWindow());

        }

        // base
        void recreateSwapChain() override//TODO update this for this app
        {
            int width = 0, height = 0;
            while (width == 0 || height == 0)
            {
                glfwGetFramebufferSize(m_context.getWindow(), &width, &height);
                glfwWaitEvents();
            }

            m_projection = glm::perspective(glm::radians(45.0f), width / static_cast<float>(height), 0.1f, 10000.0f);
            m_projection[1][1] *= -1;
            m_projectionChanged = true;
            // todo set camera width, height ?

            m_context.getDevice().waitIdle();

            cleanUpSwapchain();

            m_context.createSwapChain();
            m_context.createImageViews();
            //createRenderPass();
            //createGraphicsPipeline();
            createDepthResources();
            createSwapchainFramebuffers(m_fullscreenLightingRenderpass);
            createAllCommandBuffers();
        }

        // base
        void cleanUpSwapchain() //TODO update this for this app
        {
            m_context.getDevice().destroyImageView(m_depthImageView);
            vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);


            for (auto& scfb : m_swapChainFramebuffers)
                m_context.getDevice().destroyFramebuffer(scfb);

            m_context.getDevice().freeCommandBuffers(m_commandPool, m_commandBuffers);

            //m_context.getDevice().destroyPipeline(m_graphicsPipeline);
            //m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
            //m_context.getDevice().destroyRenderPass(m_renderpass);

            for (auto& sciv : m_context.getSwapChainImageViews())
                m_context.getDevice().destroyImageView(sciv);

            m_context.getDevice().destroySwapchainKHR(m_context.getSwapChain());
        }

        void createAllCommandBuffers()
        {
            // primary buffers, needs to be re-recorded every frame
            vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

            // static secondary buffers (containing draw calls), never change
            vk::CommandBufferAllocateInfo secondaryCmdAllocInfo(m_commandPool, vk::CommandBufferLevel::eSecondary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_gbufferSecondaryCommandBuffers            = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
            m_fullscreenLightingSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);


            // dynamic secondary buffers (containing per-frame information), getting re-recorded if necessary
            m_perFrameSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);

            

            // fill static command buffers:
            for (size_t i = 0; i < m_gbufferSecondaryCommandBuffers.size(); i++)
            {
                // gbuffer pass command buffers
                vk::CommandBufferInheritanceInfo inheritanceInfo(m_gbufferRenderpass, 0, m_gbufferFramebuffers.at(i), 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);
                m_gbufferSecondaryCommandBuffers.at(i).begin(beginInfo);

                m_gbufferSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_gbufferGraphicsPipeline);

                m_gbufferSecondaryCommandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
                m_gbufferSecondaryCommandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint32);

                m_gbufferSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_gbufferPipelineLayout, 0, 1, &m_gbufferDescriptorSets.at(0), 0, nullptr);

                m_gbufferSecondaryCommandBuffers.at(i).drawIndexedIndirect(m_indirectDrawBufferInfo.m_Buffer, 0, static_cast<uint32_t>(m_scene.getDrawCommandData().size()),
                    sizeof(std::decay_t<decltype(*m_scene.getDrawCommandData().data())>));

                m_gbufferSecondaryCommandBuffers.at(i).end();

                //TODO synchronization for g-buffer resources should be done "implicitly" by renderpasses. check this


                // fullscreen lighting pass command buffers
                vk::CommandBufferInheritanceInfo inheritanceInfo2(m_fullscreenLightingRenderpass, 0, m_swapChainFramebuffers.at(i), 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo2(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo2);
                m_fullscreenLightingSecondaryCommandBuffers.at(i).begin(beginInfo2);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_fullscreenLightingPipeline);

                // important: bind the descriptor set corresponding to the correct multi-buffered gbuffer resources
                m_fullscreenLightingSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_fullscreenLightingPipelineLayout, 0, 1, &m_fullScreenLightingDescriptorSets.at(i), 0, nullptr);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).draw(3, 1, 0, 0);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).end();
            }
        }

        void recordPerFrameCommandBuffers(uint32_t currentImage) override
        {
            ////// Secondary Command Buffer with per-frame information (TODO: this can be done in a seperate thread)
            m_perFrameSecondaryCommandBuffers.at(currentImage).reset({});

            vk::CommandBufferInheritanceInfo inheritanceInfo(m_gbufferRenderpass, 0, m_gbufferFramebuffers.at(currentImage), 0, {}, {});
            vk::CommandBufferBeginInfo beginInfo1(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);

            m_perFrameSecondaryCommandBuffers.at(currentImage).begin(beginInfo1);

            m_camera.update(m_context.getWindow());
            if(m_camera.hasChanged()) // this is currently always true
            {
                m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_gbufferPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
                m_camera.resetChangeFlag();
            }
            if (m_projectionChanged)
            {
                m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_gbufferPipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));
                m_projectionChanged = false;
            }

            m_perFrameSecondaryCommandBuffers.at(currentImage).end();


            ////// Primary Command Buffer (doesn't really change, but still needs to be re-recorded)
            m_commandBuffers.at(currentImage).reset({});

            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);
            m_commandBuffers.at(currentImage).begin(beginInfo);

            // 1st renderpass: render into g-buffer
            vk::ClearValue clearPosID(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, -1.0f });
            vk::ClearValue clearValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });

            std::array<vk::ClearValue, 4> clearColors = { clearPosID, clearValue, clearValue, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo(m_gbufferRenderpass, m_gbufferFramebuffers.at(currentImage), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());
            m_commandBuffers.at(currentImage).beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

            // execute command buffer which updates per-frame information
            m_commandBuffers.at(currentImage).executeCommands(m_perFrameSecondaryCommandBuffers.at(currentImage));

            // execute command buffers which contains rendering commands (for gbuffer)
            //TODO if this CB ever has to be recorded per-frame, it can be merged with perFrameSecondaryCommandBuffers
            m_commandBuffers.at(currentImage).executeCommands(m_gbufferSecondaryCommandBuffers.at(currentImage)); 

            m_commandBuffers.at(currentImage).endRenderPass();

            // 2nd renderpass: render into swapchain
            vk::ClearValue clearValue2(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            std::array<vk::ClearValue, 2> clearColors2 = { clearValue2, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo2(m_fullscreenLightingRenderpass, m_swapChainFramebuffers.at(currentImage), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors2.size()), clearColors2.data());
            m_commandBuffers.at(currentImage).beginRenderPass(renderpassInfo2, vk::SubpassContents::eSecondaryCommandBuffers);

            m_commandBuffers.at(currentImage).executeCommands(m_fullscreenLightingSecondaryCommandBuffers.at(currentImage));

            m_commandBuffers.at(currentImage).endRenderPass();


            m_commandBuffers.at(currentImage).end();
        }

        void configureImgui()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ////// ImGUI WINDOWS GO HERE
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("Test"))
                {
                    if(ImGui::Checkbox("Show demo window", &m_imguiShowDemoWindow)){}
                    ImGui::EndMenu();
                }

                if(m_imguiShowDemoWindow) ImGui::ShowDemoWindow();

                m_timer.drawGUI();
                ImGui::EndMainMenuBar();

            }
            /////////////////////////////

            ImGui::Render();

            if(m_imguiCommandBuffers.empty())
            {
                vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
                m_imguiCommandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);
                //TODO maybe move this
            }

        }

        void buildImguiCmdBufferAndSubmit(const uint32_t imageIndex) override
        {
            const vk::RenderPassBeginInfo imguiRenderpassInfo(m_context.getImguiRenderpass(), m_swapChainFramebuffers.at(imageIndex), { {0, 0}, m_context.getSwapChainExtent() }, 0, nullptr);

            const vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);

            // record cmd buffer
            m_imguiCommandBuffers.at(imageIndex).reset({});
            m_imguiCommandBuffers.at(imageIndex).begin(beginInfo);
            m_imguiCommandBuffers.at(imageIndex).beginRenderPass(imguiRenderpassInfo, vk::SubpassContents::eInline);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_imguiCommandBuffers.at(imageIndex));
            m_imguiCommandBuffers.at(imageIndex).endRenderPass();
            m_timer.CmdWriteTimestamp(m_imguiCommandBuffers.at(imageIndex), vk::PipelineStageFlagBits::eAllGraphics, m_queryPool);
            m_imguiCommandBuffers.at(imageIndex).end();

            // wait rest of the rendering, submit
            vk::Semaphore waitSemaphores[] = { m_graphicsRenderFinishedSemaphores.at(m_currentFrame) };
            vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
            vk::Semaphore signalSemaphores[] = { m_guiFinishedSemaphores.at(m_currentFrame) };

            const vk::SubmitInfo submitInfo(1, waitSemaphores, waitStages, 1, &m_imguiCommandBuffers.at(imageIndex), 1, signalSemaphores);

            m_context.getDevice().resetFences(m_inFlightFences.at(m_currentFrame));
            m_context.getGraphicsQueue().submit(submitInfo, m_inFlightFences.at(m_currentFrame));

        }

        void mainLoop()
        {
            while (!glfwWindowShouldClose(m_context.getWindow()))
            {
                glfwPollEvents();
                configureImgui();
                drawFrame();
                m_timer.acquireCurrentTimestamp(m_context.getDevice(), m_queryPool);
            }

            m_context.getDevice().waitIdle();
        }

    private:

        BufferInfo m_vertexBufferInfo;
        BufferInfo m_indexBufferInfo;
        BufferInfo m_indirectDrawBufferInfo;
        BufferInfo m_modelMatrixBufferInfo;



        std::vector<ImageInfo> m_allImages;
        std::vector<vk::ImageView> m_allImageViews;
        std::vector<vk::Sampler> m_allImageSamplers;

        Pilotview m_camera;
        glm::mat4 m_projection;
        bool m_projectionChanged;

        Scene m_scene;

        Timer m_timer;

        bool m_imguiShowDemoWindow = false;


        // G-Buffer Resources
        std::vector<ImageInfo> m_gbufferPositionImageInfos;
        std::vector<ImageInfo> m_gbufferNormalImageInfos;
        std::vector<ImageInfo> m_gbufferUVImageInfos;


        std::vector<vk::ImageView> m_gbufferPositionImageViews;
        std::vector<vk::ImageView> m_gbufferNormalImageViews;
        std::vector<vk::ImageView> m_gbufferUVImageViews;


        std::vector<vk::Sampler> m_gbufferPositionSamplers;
        std::vector<vk::Sampler> m_gbufferNormalSamplers;
        std::vector<vk::Sampler> m_gbufferUVSamplers;


        std::vector<ImageInfo> m_gbufferDepthImages;
        std::vector<vk::ImageView> m_gbufferDepthImageViews;

        std::vector<vk::Framebuffer> m_gbufferFramebuffers;

        // G-Buffer Descriptor Stuff
        vk::DescriptorSetLayout m_gbufferDescriptorSetLayout;
        vk::DescriptorPool m_gbufferDescriptorPool;
        std::vector<vk::DescriptorSet> m_gbufferDescriptorSets;

        // G-Buffer Renderpass, Pipeline, Secondary Command Buffer
        vk::RenderPass m_gbufferRenderpass;
        vk::PipelineLayout m_gbufferPipelineLayout;
        vk::Pipeline m_gbufferGraphicsPipeline;

        std::vector<vk::CommandBuffer> m_gbufferSecondaryCommandBuffers;


        // Fullscreen pass Renderpass, Pipeline, Secondary Command Buffer
        vk::RenderPass m_fullscreenLightingRenderpass;
        vk::PipelineLayout m_fullscreenLightingPipelineLayout;
        vk::Pipeline m_fullscreenLightingPipeline;

        std::vector<vk::CommandBuffer> m_fullscreenLightingSecondaryCommandBuffers;

        // Fullscreen Descriptor Stuff
        vk::DescriptorSetLayout m_fullScreenLightingDescriptorSetLayout;
        vk::DescriptorPool m_fullScreenLightingDescriptorPool;
        std::vector<vk::DescriptorSet> m_fullScreenLightingDescriptorSets;

        // Sync Objects
    };
}

int main()
{
    vg::DeferredApp app;

    try
    {
        app.mainLoop();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        std::terminate();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}