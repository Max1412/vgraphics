#include <iostream>
#include <filesystem>

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny/tiny_obj_loader.h"

#include "graphic/Context.h"
#include "graphic/BaseApp.h"
#include "graphic/Definitions.h"
#include "userinput/Pilotview.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace vg
{

    class App : public vg::BaseApp
    {
    public:
        App() :
			BaseApp({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters" }), 
    		m_camera(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height)
        {
            createRenderPass();
            createDescriptorSetLayout();

            createGraphicsPipeline();
            createCommandPools();

            createDepthResources();
            createFramebuffers();

            m_image = createTextureImage("chalet/chalet.jpg");
            m_textureImageMipLevels = m_image.mipLevels;
            createTextureImageView();
            createTextureSampler();

            loadModel("bunny/bunny.obj");

            createVertexBuffer();
            createIndexBuffer();
            createUniformBuffers();
            createPerFrameInformation();
            createDescriptorPool();
            createDescriptorSets();

            createCommandBuffers();
            createSyncObjects();
        }

        // todo clarify what is here and what is in cleanupswapchain
        ~App()
        {
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indexBufferInfo.m_Buffer), m_indexBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_vertexBufferInfo.m_Buffer), m_vertexBufferInfo.m_BufferAllocation);

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

            m_context.getDevice().destroySampler(m_textureSampler);
            m_context.getDevice().destroyImageView(m_textureImageView);
            vmaDestroyImage(m_context.getAllocator(), m_image.m_Image, m_image.m_ImageAllocation);

            m_context.getDevice().destroyDescriptorPool(m_descriptorPool);
            m_context.getDevice().destroyDescriptorSetLayout(m_descriptorSetLayout);

            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_uniformBufferInfos.at(i).m_Buffer), m_uniformBufferInfos.at(i).m_BufferAllocation);
            }

            m_context.getDevice().destroyPipeline(m_graphicsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
            m_context.getDevice().destroyRenderPass(m_renderpass);
            // cleanup here
        }

        void loadModel(const char* name)
        {
            //todo vertex deduplication 
            tinyobj::attrib_t attrib;
            std::vector<tinyobj::shape_t> shapes;
            std::vector<tinyobj::material_t> materials;
            std::string err;

            auto path = vg::g_resourcesPath;
            path.append(name);

            if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.string().c_str()))
            {
                throw std::runtime_error(err);
            }

            for (const auto& shape : shapes)
            {
                for (const auto& index : shape.mesh.indices)
                {
                    vg::Vertex vertex = {};

                    vertex.pos = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    };

                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1]
                    };

                    vertex.color = { 1.0f, 1.0f, 1.0f };

                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    };

                    m_vertices.push_back(vertex);
                    m_indices.push_back(static_cast<uint32_t>(m_indices.size()));
                }
            }
        }

        void createDescriptorSetLayout()
        {
            vk::DescriptorSetLayoutBinding uboLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr);

            vk::DescriptorSetLayoutBinding samplerLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

            std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_descriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

        }

        void createVertexBuffer()
        {
            m_vertexBufferInfo = fillBufferTroughStagedTransfer(m_vertices, vk::BufferUsageFlagBits::eVertexBuffer);
        }

        void createIndexBuffer()
        {
            m_indexBufferInfo = fillBufferTroughStagedTransfer(m_indices, vk::BufferUsageFlagBits::eIndexBuffer);
        }

        void createUniformBuffers()
        {
            for (size_t i = 0; i < m_context.getSwapChainImageViews().size(); i++)
            {
                auto buffer = createBuffer(sizeof(UniformBufferObject), vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_CPU_TO_GPU);
                m_uniformBufferInfos.push_back(buffer);
            }
        }

        void createPerFrameInformation() override
        {
            m_camera = Pilotview(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height);
            m_camera.setSensitivity(0.01f);
            m_projection = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10.0f);
            m_projection[1][1] *= -1;

            auto cmdBuf = beginSingleTimeCommands(m_commandPool);
            cmdBuf.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
            cmdBuf.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));
            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);

        }


        void createTextureImageView()
        {
            vk::ImageViewCreateInfo viewInfo({}, m_image.m_Image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, {}, { vk::ImageAspectFlagBits::eColor, 0, m_textureImageMipLevels, 0, 1 });
            m_textureImageView = m_context.getDevice().createImageView(viewInfo);
        }

        void createTextureSampler()
        {
            vk::SamplerCreateInfo samplerInfo({},
                vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                0.0f, VK_TRUE, 16.0f, VK_FALSE, vk::CompareOp::eAlways, 0.0f, static_cast<float>(m_textureImageMipLevels), vk::BorderColor::eIntOpaqueBlack, VK_FALSE
            );

            m_textureSampler = m_context.getDevice().createSampler(samplerInfo);
        }

        // todo change this when uniform buffer changes
        void createDescriptorPool()
        {
            // todo change descriptor count here to have as many of the type specified here as I want
            vk::DescriptorPoolSize poolSizeUniformBuffer(vk::DescriptorType::eUniformBuffer, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()));
            vk::DescriptorPoolSize poolSizeCombinedImageSampler(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()));

            std::array<vk::DescriptorPoolSize, 2> poolSizes = { poolSizeUniformBuffer, poolSizeCombinedImageSampler };

            vk::DescriptorPoolCreateInfo poolInfo({}, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()), static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            m_descriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);
        }

        void createDescriptorSets()
        {
            std::vector<vk::DescriptorSetLayout> layouts(m_swapChainFramebuffers.size(), m_descriptorSetLayout);
            vk::DescriptorSetAllocateInfo allocInfo(m_descriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), layouts.data());

            m_descriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

            for (int i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                vk::DescriptorBufferInfo bufferInfo(m_uniformBufferInfos.at(i).m_Buffer, 0, sizeof(UniformBufferObject));
                vk::WriteDescriptorSet descWrite(m_descriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr);

                vk::DescriptorImageInfo imageInfo(m_textureSampler, m_textureImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet descWriteImage(m_descriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr);

                std::array<vk::WriteDescriptorSet, 2> descriptorWrites = { descWrite, descWriteImage };

                m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
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

            m_projection = glm::perspective(glm::radians(45.0f), width / static_cast<float>(height), 0.1f, 10.0f);
            m_projection[1][1] *= -1;
            m_projectionChanged = true;

            m_context.getDevice().waitIdle();

            cleanUpSwapchain();

            m_context.createSwapChain();
            m_context.createImageViews();
            createRenderPass();
            createGraphicsPipeline();
            createDepthResources();
            createFramebuffers();
            createCommandBuffers();
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
            const auto vertShaderCode = Utility::readFile("new/shader.vert.spv");
            const auto fragShaderCode = Utility::readFile("new/shader.frag.spv");

            const auto vertShaderModule = m_context.createShaderModule(vertShaderCode);
            const auto fragShaderModule = m_context.createShaderModule(fragShaderCode);

            const vk::PipelineShaderStageCreateInfo vertShaderStageInfo({}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
            const vk::PipelineShaderStageCreateInfo fragShaderStageInfo({}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main");

            const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };


            auto bindingDescription = vg::Vertex::getBindingDescription();
            auto attributeDescriptions = vg::Vertex::getAttributeDescriptions();
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
                vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
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

        void createCommandBuffers()
        {
            vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));

            m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

            for (size_t i = 0; i < m_commandBuffers.size(); i++)
            {
                vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);

                // begin recording
                m_commandBuffers.at(i).begin(beginInfo);

                std::array<vk::ClearValue, 2> clearColors = { vk::ClearValue{std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f }}, vk::ClearDepthStencilValue{1.0f, 0} };
                vk::RenderPassBeginInfo renderpassInfo(m_renderpass, m_swapChainFramebuffers.at(i), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());

                /////////////////////////////
                // actual commands start here
                /////////////////////////////
                m_commandBuffers.at(i).beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

                m_commandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

                m_commandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
                m_commandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint32);

                m_commandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, 1, &m_descriptorSets.at(i), 0, nullptr);

                m_commandBuffers.at(i).drawIndexed(static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);

                m_commandBuffers.at(i).endRenderPass();

                // stop recording
                m_commandBuffers.at(i).end();
            }
        }

        // TODO this is bs, doesnt need to be triple buffered, can use vkCmdUpdateBuffer
        // TODO and push constants are better for view matrix I guess
        void updatePerFrameInformation(uint32_t currentImage) override
        {
            // time needed for rotation
            // todo use camera instead
            static auto startTime = std::chrono::high_resolution_clock::now();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            UniformBufferObject ubo = {};
            ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            //ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            //ubo.proj = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10.0f);

            // OpenGL space to Vulkan Space
            //ubo.proj[1][1] *= -1;

            //void* mappedData;
            //vmaMapMemory(m_context.getAllocator(), m_uniformBufferInfos.at(currentImage).m_BufferAllocation, &mappedData);
            //memcpy(mappedData, &ubo, sizeof(ubo));
            //vmaUnmapMemory(m_context.getAllocator(), m_uniformBufferInfos.at(currentImage).m_BufferAllocation);

            auto cmdBuf = beginSingleTimeCommands(m_commandPool);

            m_camera.update(m_context.getWindow());
            if(m_camera.hasChanged())
            {
                cmdBuf.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
                m_camera.resetChangeFlag();
            }

            if(m_projectionChanged)
            {
                cmdBuf.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));
                m_projectionChanged = false;
            }

            // update model matrix
            cmdBuf.updateBuffer<UniformBufferObject>(m_uniformBufferInfos.at(currentImage).m_Buffer, 0, ubo);
            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);

        }

        void mainLoop()
        {
            while (!glfwWindowShouldClose(m_context.getWindow()))
            {
                glfwPollEvents();
                drawFrame();
            }

            m_context.getDevice().waitIdle();
        }

    private:
        vk::DescriptorSetLayout m_descriptorSetLayout;

        vk::PipelineLayout m_pipelineLayout;
        vk::Pipeline m_graphicsPipeline;


        vg::BufferInfo m_vertexBufferInfo;
        BufferInfo m_indexBufferInfo;
        std::vector<BufferInfo> m_uniformBufferInfos;

        vk::DescriptorPool m_descriptorPool;
        std::vector<vk::DescriptorSet> m_descriptorSets;

        ImageInfo m_image;
        vk::ImageView m_textureImageView;
        vk::Sampler m_textureSampler;
        uint32_t m_textureImageMipLevels;

        std::vector<vg::Vertex> m_vertices;
        std::vector<uint32_t> m_indices;

        Pilotview m_camera;
        glm::mat4 m_projection;
        bool m_projectionChanged;

        // todo uniform buffer:
        // 1 big for per-mesh attributes (model matrix, later material, ...), doesnt change
        // push constants for view matrix
        // uniform buffer for projection matrix, changes only on window resize
        // important patterns:
        //      pack data together that changes together
        //      SoA over AoS

        // todo structure:
        // re-usable functions and members into baseapp, each app inherits from baseapp
        // make image/buffer creation/copy/... functions member functions of image/buffer/... classes (maybe)

    };
}

int main()
{
    vg::App app;

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