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

    class MultiApp : public BaseApp
    {
    public:
        MultiApp() :
    		BaseApp({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters" }),
    		m_camera(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height),
			m_scene("San_Miguel/san-miguel-low-poly.obj")
		{
            createRenderPass();

            createCommandPools();
            createSceneInformation("San_Miguel/");
            createDescriptorSetLayout();
            createGraphicsPipeline();

            createDepthResources();
            createSwapchainFramebuffers(m_renderpass);


            createVertexBuffer();
            createIndexBuffer();
            createIndirectDrawBuffer();
            createPerGeometryBuffers();

            createPerFrameInformation();
            createDescriptorPool();
            createDescriptorSets();

            createAllCommandBuffers();
            createSyncObjects();

            createQueryPool();

            setupImgui();
        }

        // todo clarify what is here and what is in cleanupswapchain
        ~MultiApp()
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

            for(const auto& sampler : m_allImageSamplers)
                m_context.getDevice().destroySampler(sampler);
            for (const auto& view : m_allImageViews)
                m_context.getDevice().destroyImageView(view);
            for(const auto& image : m_allImages)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);

            m_context.getDevice().destroyDescriptorPool(m_descriptorPool);
            m_context.getDevice().destroyDescriptorSetLayout(m_descriptorSetLayout);

            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_modelMatrixBufferInfo.m_Buffer), m_modelMatrixBufferInfo.m_BufferAllocation);

            m_context.getDevice().destroyPipeline(m_graphicsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
            m_context.getDevice().destroyRenderPass(m_renderpass);
            // cleanup here
        }

        void createSceneInformation(const char * foldername)
        {
			// load all images
			std::vector<ImageLoadInfo> loadedImages(m_scene.getIndexedDiffuseTexturePaths().size());
            stbi_set_flip_vertically_on_load(true);

#pragma omp parallel for
			for (int i = 0; i < m_scene.getIndexedDiffuseTexturePaths().size(); i++)
			{
                auto path = g_resourcesPath;
                const auto name = std::string(std::string(foldername) + m_scene.getIndexedDiffuseTexturePaths().at(i).second);
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
                    0.0f, VK_TRUE, 16.0f, VK_FALSE, vk::CompareOp::eAlways, 0.0f, static_cast<float>(imageInfo.mipLevels), vk::BorderColor::eIntOpaqueBlack, VK_FALSE
                );
                m_allImageSamplers.push_back(m_context.getDevice().createSampler(samplerInfo));

            }
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

        // todo change this when uniform buffer changes
        void createDescriptorPool()
        {
            // todo change descriptor count here to have as many of the type specified here as I want
            vk::DescriptorPoolSize perMeshInformationIndirectDrawSSBO(vk::DescriptorType::eStorageBuffer, 1);
            vk::DescriptorPoolSize poolSizemodelMatrixSSBO(vk::DescriptorType::eStorageBuffer, 1);
            vk::DescriptorPoolSize poolSizeCombinedImageSampler(vk::DescriptorType::eCombinedImageSampler, 1);
            vk::DescriptorPoolSize poolSizeAllImages(vk::DescriptorType::eCombinedImageSampler, 4096);

            std::array<vk::DescriptorPoolSize, 4> poolSizes = { poolSizemodelMatrixSSBO, poolSizeCombinedImageSampler, perMeshInformationIndirectDrawSSBO, poolSizeAllImages };

            vk::DescriptorPoolCreateInfo poolInfo({}, 1, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            m_descriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);
        }

        void createDescriptorSets()
        {
            // only 1 set needed
            vk::DescriptorSetAllocateInfo allocInfo(m_descriptorPool, 1, &m_descriptorSetLayout);
            m_descriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

            // model matrix buffer
            vk::DescriptorBufferInfo bufferInfo(m_modelMatrixBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWrite(m_descriptorSets.at(0), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo, nullptr);

            // indirect draw buffer + add. info as ssbo
            vk::DescriptorBufferInfo perMeshInformationIndirectDrawSSBOInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWrite2(m_descriptorSets.at(0), 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &perMeshInformationIndirectDrawSSBOInfo, nullptr);

            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for(int i = 0; i < m_allImages.size(); i++)
            {
                allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::WriteDescriptorSet descWriteAllImages(m_descriptorSets.at(0), 3, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

            std::array<vk::WriteDescriptorSet, 3> descriptorWrites = { descWrite, descWrite2, descWriteAllImages };
            m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }

        void createDescriptorSetLayout()
        {
            vk::DescriptorSetLayoutBinding modelMatrixSSBOLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr);
            vk::DescriptorSetLayoutBinding perMeshInformationIndirectDrawSSBOLB(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

            vk::DescriptorSetLayoutBinding samplerLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

            vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImages.size()), vk::ShaderStageFlagBits::eFragment, nullptr);


            std::array<vk::DescriptorSetLayoutBinding, 4> bindings = { modelMatrixSSBOLayoutBinding, samplerLayoutBinding, perMeshInformationIndirectDrawSSBOLB, allTexturesLayoutBinding };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_descriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);
        }


        // base
        void recreateSwapChain() override
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
            createRenderPass();
            createGraphicsPipeline();
            createDepthResources();
            createSwapchainFramebuffers(m_renderpass);
            createAllCommandBuffers();
        }

        // base
        void cleanUpSwapchain()
        {
            m_context.getDevice().destroyImageView(m_depthImageView);
            vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);


            for (auto& scfb : m_swapChainFramebuffers)
                m_context.getDevice().destroyFramebuffer(scfb);

            m_context.getDevice().freeCommandBuffers(m_commandPool, m_commandBuffers);

            m_context.getDevice().destroyPipeline(m_graphicsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
            m_context.getDevice().destroyRenderPass(m_renderpass);

            for (auto& sciv : m_context.getSwapChainImageViews())
                m_context.getDevice().destroyImageView(sciv);

            m_context.getDevice().destroySwapchainKHR(m_context.getSwapChain());
        }

        void createGraphicsPipeline()
        {
            const auto vertShaderCode = Utility::readFile("multi/shader.vert.spv");
            const auto fragShaderCode = Utility::readFile("multi/shader.frag.spv");

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
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 1, &bindingDescription, static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

            vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_context.getWidth()), static_cast<float>(m_context.getHeight()), 0.0f, 1.0f);

            vk::Rect2D scissor({ 0, 0 }, m_context.getSwapChainExtent());

            vk::PipelineViewportStateCreateInfo viewportState({}, 1, &viewport, 1, &scissor);

            vk::PipelineRasterizationStateCreateInfo rasterizer({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);

            vk::PipelineMultisampleStateCreateInfo mulitsampling({}, vk::SampleCountFlagBits::e1, VK_FALSE);

            vk::PipelineDepthStencilStateCreateInfo depthStencil({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess);

            vk::PipelineColorBlendAttachmentState colorBlendAttachment; // standard values for blending.
            colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            colorBlendAttachment.setBlendEnable(VK_FALSE);
            // to enable blending, translate the following into hpp code: (and also use logic op copy in the struct below)
            //colorBlendAttachment.blendEnable = VK_TRUE;
            //colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            //colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            //colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            //colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            //colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            //colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            vk::PipelineColorBlendStateCreateInfo colorBlending; // standard values for now
            colorBlending.setLogicOpEnable(VK_FALSE);
            colorBlending.setLogicOp(vk::LogicOp::eCopy);
            colorBlending.attachmentCount = 1;
            colorBlending.setPAttachments(&colorBlendAttachment);
            colorBlending.setBlendConstants(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

            //std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };

            //vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates.size(), dynamicStates.data());

            std::array<vk::PushConstantRange, 1> vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, 2 * sizeof(glm::mat4)},
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPushConstantRangeCount(static_cast<uint32_t>(vpcr.size()));
            pipelineLayoutInfo.setPPushConstantRanges(vpcr.data());
            pipelineLayoutInfo.setPSetLayouts(&m_descriptorSetLayout);

            m_pipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);

            vk::GraphicsPipelineCreateInfo pipelineInfo;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &mulitsampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = nullptr;// &dynamicState;

            pipelineInfo.layout = m_pipelineLayout;
            pipelineInfo.renderPass = m_renderpass;
            pipelineInfo.subpass = 0;   // this is an index
            // missing: pipeline derivation

            m_graphicsPipeline = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);

            m_context.getDevice().destroyShaderModule(vertShaderModule);
            m_context.getDevice().destroyShaderModule(fragShaderModule);
        }

        void createRenderPass()
        {
            vk::AttachmentDescription colorAttachment({}, m_context.getSwapChainImageFormat(),
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal
            );

            vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

            vk::AttachmentDescription depthAttachment({}, vk::Format::eD32SfloatS8Uint,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal
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

            m_renderpass = m_context.getDevice().createRenderPass(renderpassInfo);

        }

        void createAllCommandBuffers()
        {
            // primary buffers, needs to be re-recorded every frame
            vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

            // static secondary buffers (containing draw calls), never change
            vk::CommandBufferAllocateInfo secondaryCmdAllocInfo(m_commandPool, vk::CommandBufferLevel::eSecondary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_staticSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);

            // dynamic secondary buffers (containing per-frame information), getting re-recorded if necessary
            m_perFrameSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);

            // fill static command buffers:
            for (size_t i = 0; i < m_staticSecondaryCommandBuffers.size(); i++)
            {
                vk::CommandBufferInheritanceInfo inheritanceInfo(m_renderpass, 0, m_swapChainFramebuffers.at(i), 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);
                m_staticSecondaryCommandBuffers.at(i).begin(beginInfo);


                m_staticSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

                m_staticSecondaryCommandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
                m_staticSecondaryCommandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint32);

                m_staticSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, 1, &m_descriptorSets.at(0), 0, nullptr);

                m_staticSecondaryCommandBuffers.at(i).drawIndexedIndirect(m_indirectDrawBufferInfo.m_Buffer, 0, static_cast<uint32_t>(m_scene.getDrawCommandData().size()),
                    sizeof(std::decay_t<decltype(*m_scene.getDrawCommandData().data())>));


                m_staticSecondaryCommandBuffers.at(i).end();
            }
        }

        void recordPerFrameCommandBuffers(uint32_t currentImage) override
        {
            ////// Secondary Command Buffer with per-frame information (TODO: this can be done in a seperate thread)
            m_perFrameSecondaryCommandBuffers.at(currentImage).reset({});

            vk::CommandBufferInheritanceInfo inheritanceInfo(m_renderpass, 0, m_swapChainFramebuffers.at(currentImage), 0, {}, {});
            vk::CommandBufferBeginInfo beginInfo1(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);

            m_perFrameSecondaryCommandBuffers.at(currentImage).begin(beginInfo1);

            m_camera.update(m_context.getWindow());
            if(m_camera.hasChanged()) // this is currently always true
            {
                m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
                m_camera.resetChangeFlag();
            }
            if (m_projectionChanged)
            {
                m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));
                m_projectionChanged = false;
            }

            m_perFrameSecondaryCommandBuffers.at(currentImage).end();


            ////// Primary Command Buffer (doesn't really change, but still needs to be re-recorded)
            m_commandBuffers.at(currentImage).reset({});

            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);
            m_commandBuffers.at(currentImage).begin(beginInfo);

            std::array<vk::ClearValue, 2> clearColors = { vk::ClearValue{std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f }}, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo(m_renderpass, m_swapChainFramebuffers.at(currentImage), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());
            m_commandBuffers.at(currentImage).beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

            // execute command buffer which updates per-frame information
            m_commandBuffers.at(currentImage).executeCommands(m_perFrameSecondaryCommandBuffers.at(currentImage));

            // execute command buffers which contains rendering commands
            m_commandBuffers.at(currentImage).executeCommands(m_staticSecondaryCommandBuffers.at(currentImage));

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

        vk::RenderPass m_renderpass;

        vk::DescriptorSetLayout m_descriptorSetLayout;

        vk::PipelineLayout m_pipelineLayout;
        vk::Pipeline m_graphicsPipeline;


        BufferInfo m_vertexBufferInfo;
        BufferInfo m_indexBufferInfo;
        BufferInfo m_indirectDrawBufferInfo;
        BufferInfo m_modelMatrixBufferInfo;

        vk::DescriptorPool m_descriptorPool;
        std::vector<vk::DescriptorSet> m_descriptorSets;


        std::vector<ImageInfo> m_allImages;
        std::vector<vk::ImageView> m_allImageViews;
        std::vector<vk::Sampler> m_allImageSamplers;

        Pilotview m_camera;
        glm::mat4 m_projection;
        bool m_projectionChanged;

        Scene m_scene;

        Timer m_timer;

        bool m_imguiShowDemoWindow = false;
    };
}

int main()
{
    vg::MultiApp app;

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