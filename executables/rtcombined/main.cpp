#include <iostream>
#include <filesystem>

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_major_storage.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#include "graphic/Context.h"
#include "graphic/BaseApp.h"
#include "graphic/Definitions.h"
#include "userinput/Pilotview.h"
#include "geometry/PBRScene.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_glfw.h"
#include "utility/Timer.h"
#include "stb/stb_image.h"
#include "geometry/lightmanager.h"
#include <random>
#include <execution>


namespace vg
{

    class RTCombinedApp : public BaseApp
    {
    public:
        RTCombinedApp() :
            BaseApp({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters", "VK_NV_ray_tracing" }),
            m_camera(m_context.getSwapChainExtent().width,
                m_context.getSwapChainExtent().height),
            m_timerManager(std::map<std::string, Timer>{
                { {"0 AS Update"}, Timer{ false } },
                { {"1 G-Buffer"}, {} },
                { {"2 Ray Traced Shadows"}, {} },
                { {"3 Ray Traced Ambient Occlusion"}, {} },
                { {"4 Ray Traced Reflections"}, {} },
                { {"5 Fullscreen Lighting"}, {} },
                { {"6 ImGui"}, {} },
                { {"AS Build"},  Timer{ false } }
            }, m_context),
            m_scene("pica_pica_-_mini_diorama_01/scene.gltf")
            //m_scene("Bistro/Bistro_Research_Exterior.fbx")
        {
            createCommandPools();
		    createSceneInformation("pica_pica_-_mini_diorama_01/");
			//createSceneInformation("Bistro/");

            createDepthResources();

            createGBufferRenderpass();
            createFullscreenLightingRenderpass();

            createSwapchainFramebuffers(m_fullscreenLightingRenderpass);
            createGBufferResources();

            createVertexBuffer();
            createIndexBuffer();
            createIndirectDrawBuffer();
            createPerGeometryBuffers();
            createMaterialBuffer();

            createCombinedDescriptorPool();

            createLightStuff();
            createRTResources();
            createRTDescriptors();

            createGBufferDescriptors();
            createFullscreenLightingDescriptors();

            createGBufferPipeline();
            createFullscreenLightingPipeline();

            createRandomImage();

            // RT
            createAccelerationStructure();
            createRTSoftShadowsPipeline();
            createRTAOPipeline();
			createRTReflectionPipeline();

            createPerFrameInformation();

            createAllCommandBuffers();
            createSyncObjects();

            createQueryPool();

            setupImgui();
        }

        // todo clarify what is here and what is in cleanupswapchain
        ~RTCombinedApp()
        {
            m_context.getDevice().destroyQueryPool(m_queryPool);

            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indexBufferInfo.m_Buffer), m_indexBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_vertexBufferInfo.m_Buffer), m_vertexBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indirectDrawBufferInfo.m_Buffer), m_indirectDrawBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_materialBufferInfo.m_Buffer), m_materialBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_modelMatrixBufferInfo.m_Buffer), m_modelMatrixBufferInfo.m_BufferAllocation);

            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_scratchBuffer.m_Buffer), m_scratchBuffer.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_instanceBufferInfo.m_Buffer), m_instanceBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_offsetBufferInfo.m_Buffer), m_offsetBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_rtSoftShadowSBTInfo.m_Buffer), m_rtSoftShadowSBTInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_rtAOSBTInfo.m_Buffer), m_rtAOSBTInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_rtReflectionsSBTInfo.m_Buffer), m_rtReflectionsSBTInfo.m_BufferAllocation);


            m_context.getDevice().destroyAccelerationStructureNV(m_topAS.m_AS);
            vmaFreeMemory(m_context.getAllocator(), m_topAS.m_BufferAllocation);

            for (const auto& as : m_bottomASs)
            {
                m_context.getDevice().destroyAccelerationStructureNV(as.m_AS);
                vmaFreeMemory(m_context.getAllocator(), as.m_BufferAllocation);
            }

            m_context.getDevice().destroyPipeline(m_rtSoftShadowsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_rtSoftShadowsPipelineLayout);

            m_context.getDevice().destroyPipeline(m_rtAOPipeline);
            m_context.getDevice().destroyPipelineLayout(m_rtAOPipelineLayout);

            m_context.getDevice().destroyPipeline(m_rtReflectionsPipeline);
            m_context.getDevice().destroyPipelineLayout(m_rtReflectionsPipelineLayout);

            for(const auto& buffer : m_lightBufferInfos)
                vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(buffer.m_Buffer), buffer.m_BufferAllocation);

            for (const auto& buffer : m_rtPerFrameInfoBufferInfos)
                vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(buffer.m_Buffer), buffer.m_BufferAllocation);            

            m_context.getDevice().destroyImageView(m_depthImageView);
            vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);

            for (int i = 0; i < m_context.max_frames_in_flight; i++)
            {
                m_context.getDevice().destroySemaphore(m_imageAvailableSemaphores.at(i));
                m_context.getDevice().destroySemaphore(m_graphicsRenderFinishedSemaphores.at(i));
                m_context.getDevice().destroySemaphore(m_guiFinishedSemaphores.at(i));
                m_context.getDevice().destroyFence(m_inFlightFences.at(i));
            }

            for(const auto& fence : m_computeFinishedFences)
                m_context.getDevice().destroyFence(fence);

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

            for (const auto& image : m_rtSoftShadowDirectionalImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtSoftShadowDirectionalImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtSoftShadowDirectionalImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_rtSoftShadowPointImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtSoftShadowPointImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtSoftShadowPointImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_rtSoftShadowSpotImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtSoftShadowSpotImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtSoftShadowSpotImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_rtAOImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtAOImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtAOImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_rtReflectionImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtReflectionImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtReflectionImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_rtReflectionLowResImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);
            for (const auto& view : m_rtReflectionLowResImageViews)
                m_context.getDevice().destroyImageView(view);
            for (const auto& sampler : m_rtReflectionLowResImageSamplers)
                m_context.getDevice().destroySampler(sampler);

            for (const auto& image : m_randomImageInfos)
                vmaDestroyImage(m_context.getAllocator(), image.m_Image, image.m_ImageAllocation);

            for (const auto& view : m_randomImageViews)
                m_context.getDevice().destroyImageView(view);

            m_context.getDevice().destroyDescriptorPool(m_combinedDescriptorPool);

            m_context.getDevice().destroyDescriptorSetLayout(m_gbufferDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_fullScreenLightingDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_lightDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_rtSoftShadowsDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_allRTImageSampleDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_shadowImageStoreDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_rtAOImageStoreDescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_rtAODescriptorSetLayout);
            m_context.getDevice().destroyDescriptorSetLayout(m_rtReflectionsDescriptorSetLayout);

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
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal //TODO is this the right layout? maybe colorattachmentoptimal
            );

            vk::AttachmentReference positionAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

            vk::AttachmentDescription normalAttachment({}, vk::Format::eR32G32B32A32Sfloat, //TODO use saved format, not hard-coded
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal //TODO is this the right layout? maybe colorattachmentoptimal
            );

            vk::AttachmentReference normalAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal);

            vk::AttachmentDescription uvAttachment({}, vk::Format::eR32G32B32A32Sfloat, //TODO use saved format, not hard-coded
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal //TODO is this the right layout? maybe colorattachmentoptimal
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

            std::array colorAttachmentRefs = { positionAttachmentRef, normalAttachmentRef, uvAttachmentRef };

            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics,
                0, nullptr,                 // input attachments (standard values)
                static_cast<uint32_t>(colorAttachmentRefs.size()), colorAttachmentRefs.data(),     // color attachments: layout (location = 0) out -> colorAttachmentRef is at index 0
                nullptr,                    // no resolve attachment
                &depthAttachmentRef);       // depth stencil attachment
                                            // other attachment at standard values: Preserved

            std::array attachments = { positionAttachment, normalAttachment, uvAttachment, depthAttachment };

            vk::RenderPassCreateInfo renderpassInfo({}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1, &subpass, 1, &dependency);

            m_gbufferRenderpass = m_context.getDevice().createRenderPass(renderpassInfo);

        }

        void createCombinedDescriptorPool()
        {
            // 2: create descriptor pool
            vk::DescriptorPoolSize poolSizeAllImages(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImageSamplers.size()));
            vk::DescriptorPoolSize poolSizeForSSBOs(vk::DescriptorType::eStorageBuffer, 7);
            vk::DescriptorPoolSize gbufferImages(vk::DescriptorType::eCombinedImageSampler, 3);
            vk::DescriptorPoolSize shadowImage(vk::DescriptorType::eStorageImage, 1);
            vk::DescriptorPoolSize rtOutputImage(vk::DescriptorType::eStorageImage, 1);
            vk::DescriptorPoolSize rtAS(vk::DescriptorType::eAccelerationStructureNV, 1);

            std::array poolSizes = { poolSizeForSSBOs, gbufferImages, poolSizeAllImages, rtOutputImage, rtAS, shadowImage };


            vk::DescriptorPoolCreateInfo poolInfo({}, 7 * static_cast<uint32_t>(m_swapChainFramebuffers.size()) + 2, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            m_combinedDescriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);
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

            

            // 3: create descriptor set

            vk::DescriptorSetAllocateInfo allocInfo(m_combinedDescriptorPool, 1, &m_gbufferDescriptorSetLayout);
            m_gbufferDescriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

            // model matrix buffer
            vk::DescriptorBufferInfo bufferInfo(m_modelMatrixBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWrite(m_gbufferDescriptorSets.at(0), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo, nullptr);
            vk::DescriptorBufferInfo perMeshInformationIndirectDrawSSBOInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWritePerMeshInfo(m_gbufferDescriptorSets.at(0), 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &perMeshInformationIndirectDrawSSBOInfo, nullptr);

            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for (size_t i = 0; i < m_allImages.size(); i++)
            {
                allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::WriteDescriptorSet descWriteAllImages(m_gbufferDescriptorSets.at(0), 1, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

            std::array descriptorWrites = { descWrite, descWriteAllImages, descWritePerMeshInfo };
            m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }

        void createGBufferPipeline()
        {
            const auto vertShaderCode = Utility::readFile("combined/gbuffer.vert.spv");
            const auto fragShaderCode = Utility::readFile("combined/gbuffer.frag.spv");

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
            std::array blendAttachments = { colorBlendAttachment, colorBlendAttachment, colorBlendAttachment };
            // standard values for now
            vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy,
                static_cast<uint32_t>(blendAttachments.size()), blendAttachments.data(),
                std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
            
            // no dynamic state needed
            //std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
            //vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates.size(), dynamicStates.data());

            // push view & proj matrix
            std::array vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(float) + sizeof(int32_t)}
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
                std::array attachments = {
                    m_gbufferPositionImageViews.at(i),
                    m_gbufferNormalImageViews.at(i),
                    m_gbufferUVImageViews.at(i),
                    m_gbufferDepthImageViews.at(i)
                }; //TODO what depth image to use?

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

            //TODO is this correct?
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
            const auto fragShaderCode = Utility::readFile("combined/fullscreenLightingPBR_RT.frag.spv");

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
            std::array vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(float) + sizeof(int32_t)}
            };

            std::array dsls = { m_fullScreenLightingDescriptorSetLayout, m_lightDescriptorSetLayout, m_allRTImageSampleDescriptorSetLayout };
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, static_cast<uint32_t>(dsls.size()), dsls.data(), static_cast<uint32_t>(vpcr.size()), vpcr.data());

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
            vk::DescriptorSetLayoutBinding materialSSBOBinding(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

            std::array bindings = {
                perMeshInformationIndirectDrawSSBOLB,
                allTexturesLayoutBinding,
                positionTextureBinding,
                normalTextureBinding,
                uvTextureBinding,
                materialSSBOBinding
            };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_fullScreenLightingDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);



            // create n descriptor sets, 1 for each multi-buffered gbuffer
            std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_fullScreenLightingDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
            m_fullScreenLightingDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);


            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for (size_t i = 0; i < m_allImages.size(); i++)
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

                vk::DescriptorBufferInfo materialInfo(m_materialBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet descWriteMaterialInfo(m_fullScreenLightingDescriptorSets.at(i), 5, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &materialInfo, nullptr);

                std::array descriptorWrites = { 
                    descWritePerMeshInfo, 
                    descWriteAllImages,
                    descWriteGBufferPos,
                    descWriteGBufferNormal,
                    descWriteGBufferUV,
                    descWriteMaterialInfo
                };

                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        
        }

        void createSceneInformation(const char * foldername)
        {
            m_context.getLogger()->info("Loading Textures...");
            // load all images
            std::vector<ImageLoadInfo> loadedImages(m_scene.getIndexedBaseColorTexturePaths().size() + m_scene.getIndexedMetallicRoughnessTexturePaths().size());
            stbi_set_flip_vertically_on_load(true);

#pragma omp parallel for
            for (int i = 0; i < static_cast<int>(m_scene.getIndexedBaseColorTexturePaths().size()); i++)
            {
                auto path = g_resourcesPath;
                const auto name = std::string(std::string(foldername) + m_scene.getIndexedBaseColorTexturePaths().at(i).second);
                path.append(name);
                loadedImages.at(i).pixels = stbi_load(path.string().c_str(), &loadedImages.at(i).texWidth, &loadedImages.at(i).texHeight, &loadedImages.at(i).texChannels, STBI_rgb_alpha);
                loadedImages.at(i).mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(loadedImages.at(i).texWidth, loadedImages.at(i).texHeight)))) + 1;
            }

#pragma omp parallel for
            for (int i = static_cast<int>(m_scene.getIndexedBaseColorTexturePaths().size()); i < static_cast<int>(loadedImages.size()); i++)
            {
                auto path = g_resourcesPath;
                const auto name = std::string(std::string(foldername) + m_scene.getIndexedMetallicRoughnessTexturePaths().at(i - m_scene.getIndexedBaseColorTexturePaths().size()).second);
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

				stbi_image_free(ili.pixels);                
            }

            m_context.getLogger()->info("Texture loading complete.");
        }

        void createLightStuff()
        {
            PBRDirectionalLight dirLight;
            dirLight.intensity = glm::vec3(0.0f);
            dirLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);

            PBRPointLight pointLight;
            pointLight.position = glm::vec3(43.0f, 100.0f, -17.0f);
            pointLight.intensity = glm::vec3(8000.0f);
            pointLight.radius = 5.0f;

            PBRSpotLight spotLight;
            spotLight.position = glm::vec3(3.0f, 10.0f, 3.0f);
            spotLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
            spotLight.intensity = glm::vec3(0.0f);
            spotLight.cutoff = 1.0f;
            spotLight.outerCutoff = 0.75f;
            spotLight.radius = 1.0f;

            m_lightManager = PBRLightManager(std::vector<PBRDirectionalLight>{dirLight}, std::vector<PBRPointLight>{pointLight}, std::vector<PBRSpotLight>{spotLight});

            
            // create buffers for lights. buffers are persistently mapped //TODO make lightmanager manage the light buffers with functions for access
            m_lightBufferInfos.push_back(createBuffer(sizeof(DirectionalLight) * m_lightManager.getDirectionalLights().size(), vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode::eExclusive, VMA_ALLOCATION_CREATE_MAPPED_BIT));
            memcpy(m_lightBufferInfos.at(0).m_BufferAllocInfo.pMappedData, m_lightManager.getDirectionalLights().data(), sizeof(DirectionalLight) * m_lightManager.getDirectionalLights().size());

            m_lightBufferInfos.push_back(createBuffer(sizeof(PointLight) * m_lightManager.getPointLights().size(), vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode::eExclusive, VMA_ALLOCATION_CREATE_MAPPED_BIT));
            memcpy(m_lightBufferInfos.at(1).m_BufferAllocInfo.pMappedData, m_lightManager.getPointLights().data(), sizeof(PointLight) * m_lightManager.getPointLights().size());

            m_lightBufferInfos.push_back(createBuffer(sizeof(SpotLight) * m_lightManager.getSpotLights().size(), vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode::eExclusive, VMA_ALLOCATION_CREATE_MAPPED_BIT));
            memcpy(m_lightBufferInfos.at(2).m_BufferAllocInfo.pMappedData, m_lightManager.getSpotLights().data(), sizeof(SpotLight) * m_lightManager.getSpotLights().size());


            // create light descriptor set layout, descriptor set
            vk::DescriptorSetLayoutBinding dirLights(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
            vk::DescriptorSetLayoutBinding pointLights(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
            vk::DescriptorSetLayoutBinding spotLights(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);

            std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {
                dirLights, pointLights, spotLights
            };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_lightDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, 1, &m_lightDescriptorSetLayout);
            m_lightDescriptorSet = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo).at(0);

            vk::DescriptorBufferInfo dirLightsInfo(m_lightBufferInfos.at(0).m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWriteDirLights(m_lightDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &dirLightsInfo, nullptr);
            
            vk::DescriptorBufferInfo pointLightsInfo(m_lightBufferInfos.at(1).m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWritePointLights(m_lightDescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pointLightsInfo, nullptr);

            vk::DescriptorBufferInfo spotLightsInfo(m_lightBufferInfos.at(2).m_Buffer, 0, VK_WHOLE_SIZE);
            vk::WriteDescriptorSet descWriteSpotLights(m_lightDescriptorSet, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &spotLightsInfo, nullptr);

            std::array<vk::WriteDescriptorSet, 3> descriptorWrites = {
                descWriteDirLights,
                descWritePointLights,
                descWriteSpotLights,
            };

            m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);

        }


        void createRTResources()
        {
            const auto ext = m_context.getSwapChainExtent();
            auto cmdBuf = beginSingleTimeCommands(m_commandPool);

         

            // soft shadow image : layered float32 image.
            // rtao image: float32 image

            // multi-buffered for whatever reason

            for (size_t i = 0; i < m_context.getSwapChainImages().size(); i++)
            {
                // shadow image arrays (layered images)
                m_rtSoftShadowDirectionalImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        vk::SharingMode::eExclusive, 0,
                        static_cast<int32_t>(m_lightManager.getDirectionalLights().size()))
                );

                m_rtSoftShadowPointImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        vk::SharingMode::eExclusive, 0,
                        static_cast<int32_t>(m_lightManager.getPointLights().size()))
                );

                m_rtSoftShadowSpotImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        vk::SharingMode::eExclusive, 0,
                        static_cast<int32_t>(m_lightManager.getPointLights().size()))
                );

                const vk::ImageViewCreateInfo rtShadowDirectional({},
                    m_rtSoftShadowDirectionalImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2DArray,
                    vk::Format::eR32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_rtSoftShadowDirectionalImageViews.push_back(m_context.getDevice().createImageView(rtShadowDirectional));

                const vk::ImageViewCreateInfo rtShadowPoint({},
                    m_rtSoftShadowPointImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2DArray,
                    vk::Format::eR32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_rtSoftShadowPointImageViews.push_back(m_context.getDevice().createImageView(rtShadowPoint));

                const vk::ImageViewCreateInfo rtShadowSpot({},
                    m_rtSoftShadowSpotImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2DArray,
                    vk::Format::eR32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_rtSoftShadowSpotImageViews.push_back(m_context.getDevice().createImageView(rtShadowSpot));

                vk::SamplerCreateInfo samplerRTShadowDirectional({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, //TODO maybe actually filter those, especially when using half-res
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_rtSoftShadowDirectionalImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_rtSoftShadowDirectionalImageSamplers.push_back(m_context.getDevice().createSampler(samplerRTShadowDirectional));

                vk::SamplerCreateInfo samplerRTShadowPoint({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, //TODO maybe actually filter those, especially when using half-res
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_rtSoftShadowPointImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_rtSoftShadowPointImageSamplers.push_back(m_context.getDevice().createSampler(samplerRTShadowPoint));

                vk::SamplerCreateInfo samplerRTShadowSpot({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, //TODO maybe actually filter those, especially when using half-res
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_rtSoftShadowSpotImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_rtSoftShadowSpotImageSamplers.push_back(m_context.getDevice().createSampler(samplerRTShadowSpot));

                // rtao images
                m_rtAOImageInfos.push_back(
                    createImage(ext.width, ext.height, 1,
                        vk::Format::eR32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        vk::SharingMode::eExclusive, 0,
                        1)
                );

                const vk::ImageViewCreateInfo rtAOPoint({},
                    m_rtAOImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_rtAOImageViews.push_back(m_context.getDevice().createImageView(rtAOPoint));

                vk::SamplerCreateInfo samplerRTAOPoint({},
                    vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, //TODO maybe actually filter those, especially when using half-res
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_rtAOImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_rtAOImageSamplers.push_back(m_context.getDevice().createSampler(samplerRTAOPoint));

				// reflection images
				m_rtReflectionImageInfos.push_back(
					createImage(ext.width, ext.height, 1,
						vk::Format::eR32G32B32A32Sfloat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
						VMA_MEMORY_USAGE_GPU_ONLY,
						vk::SharingMode::eExclusive, 0,
						1)
				);

				const vk::ImageViewCreateInfo rtReflectionsView({},
					m_rtReflectionImageInfos.at(i).m_Image,
					vk::ImageViewType::e2D,
					vk::Format::eR32G32B32A32Sfloat,
					{},
					{ vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
				m_rtReflectionImageViews.push_back(m_context.getDevice().createImageView(rtReflectionsView));

				vk::SamplerCreateInfo samplerRTReflections({},
					vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, //TODO maybe actually filter those, especially when using half-res
					vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
					0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
					static_cast<float>(m_rtReflectionImageInfos.at(i).mipLevels),
					vk::BorderColor::eIntOpaqueBlack, false);
				m_rtReflectionImageSamplers.push_back(m_context.getDevice().createSampler(samplerRTReflections));

                // reflection images LOW RES
                m_rtReflectionLowResImageInfos.push_back(
                    createImage(ext.width/2, ext.height/2, 1,
                        vk::Format::eR32G32B32A32Sfloat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        vk::SharingMode::eExclusive, 0,
                        1)
                );

                const vk::ImageViewCreateInfo rtReflectionsLowResView({},
                    m_rtReflectionLowResImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32G32B32A32Sfloat,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_rtReflectionLowResImageViews.push_back(m_context.getDevice().createImageView(rtReflectionsLowResView));

                vk::SamplerCreateInfo samplerLowResRTReflections({},
                    vk::Filter::eNearest, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, //TODO maybe actually filter those, especially when using half-res
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 1.0f, false, vk::CompareOp::eAlways, 0.0f,
                    static_cast<float>(m_rtReflectionLowResImageInfos.at(i).mipLevels),
                    vk::BorderColor::eIntOpaqueBlack, false);
                m_rtReflectionLowResImageSamplers.push_back(m_context.getDevice().createSampler(samplerLowResRTReflections));


                // transition images to use them for the first time

                vk::ImageMemoryBarrier barrierShadowSpotTOFS(
                    {}, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_rtSoftShadowSpotImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                vk::ImageMemoryBarrier barrierShadowPointTOFS = barrierShadowSpotTOFS;
                barrierShadowPointTOFS.image = m_rtSoftShadowPointImageInfos.at(i).m_Image;

                vk::ImageMemoryBarrier barrierShadowDirectionalTOFS = barrierShadowSpotTOFS;
                barrierShadowDirectionalTOFS.image = m_rtSoftShadowDirectionalImageInfos.at(i).m_Image;

                vk::ImageMemoryBarrier barrierAOTOFS = barrierShadowSpotTOFS;
                barrierAOTOFS.image = m_rtAOImageInfos.at(i).m_Image;

				vk::ImageMemoryBarrier barrierReflectionTOFS = barrierShadowSpotTOFS;
				barrierReflectionTOFS.image = m_rtReflectionImageInfos.at(i).m_Image;

                vk::ImageMemoryBarrier barrierLowResReflectionTOFS = barrierShadowSpotTOFS;
                barrierLowResReflectionTOFS.image = m_rtReflectionLowResImageInfos.at(i).m_Image;

                std::array barriers = { barrierShadowSpotTOFS, barrierShadowPointTOFS, barrierShadowDirectionalTOFS, barrierAOTOFS, barrierReflectionTOFS, barrierLowResReflectionTOFS };

                cmdBuf.pipelineBarrier(
                    vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::DependencyFlagBits::eByRegion, {}, {}, barriers
                );


            }
            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);

          
        }

        void createRTDescriptors()
        {
            // inputs
            using ssf = vk::ShaderStageFlagBits;
            vk::DescriptorSetLayoutBinding shadowDirImageBinding  (0, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding shadowPointImageBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding shadowSpotImageBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding rtAOImageBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);
			vk::DescriptorSetLayoutBinding rtReflectionImageBinding(4, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);
            vk::DescriptorSetLayoutBinding rtReflectionLowResImageBinding(5, vk::DescriptorType::eCombinedImageSampler, 1, ssf::eFragment, nullptr);

            std::array bindings = {
                shadowDirImageBinding,
                shadowPointImageBinding,
                shadowSpotImageBinding,
                rtAOImageBinding,
				rtReflectionImageBinding,
                rtReflectionLowResImageBinding
            };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_allRTImageSampleDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            // create n descriptor sets, 1 for each multi-buffered gbuffer
            std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_allRTImageSampleDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
            m_allRTImageSampleDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);


            vk::DescriptorSetLayoutBinding shadowDirImageBinding1(0, vk::DescriptorType::eStorageImage, 1, ssf::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding shadowPointImageBinding1(1, vk::DescriptorType::eStorageImage, 1,  ssf::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding shadowSpotImageBinding1(2, vk::DescriptorType::eStorageImage, 1, ssf::eRaygenNV, nullptr);

            std::array bindings1 = {
                shadowDirImageBinding1,
                shadowPointImageBinding1,
                shadowSpotImageBinding1
            };

            vk::DescriptorSetLayoutCreateInfo layoutInfo1({}, static_cast<uint32_t>(bindings1.size()), bindings1.data());

            m_shadowImageStoreDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo1);

            // create n descriptor sets, 1 for each multi-buffered gbuffer
            std::vector<vk::DescriptorSetLayout> dsls1(m_swapChainFramebuffers.size(), m_shadowImageStoreDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo1(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls1.data());
            m_shadowImageStoreDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo1);

            vk::DescriptorSetLayoutBinding rtAOImageStoreBinding(0, vk::DescriptorType::eStorageImage, 1, ssf::eRaygenNV, nullptr);

            std::array bindings2 = {
                rtAOImageStoreBinding
            };

            vk::DescriptorSetLayoutCreateInfo layoutInfo2({}, static_cast<uint32_t>(bindings2.size()), bindings2.data());
            m_rtAOImageStoreDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo2);


            std::vector<vk::DescriptorSetLayout> dsls2(m_swapChainFramebuffers.size(), m_rtAOImageStoreDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo2(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls2.data());
            m_rtAOImageStoreDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo2);
            
            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {

                // sampling descriptor set
                vk::DescriptorImageInfo shadowDirSampleImageInfo(m_rtSoftShadowDirectionalImageSamplers.at(i), m_rtSoftShadowDirectionalImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet shadowDirSampleImageWrite(m_allRTImageSampleDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shadowDirSampleImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo shadowPointSampleImageInfo(m_rtSoftShadowPointImageSamplers.at(i), m_rtSoftShadowPointImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet shadowPointSampleImageWrite(m_allRTImageSampleDescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shadowPointSampleImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo shadowSpotSampleImageInfo(m_rtSoftShadowSpotImageSamplers.at(i), m_rtSoftShadowSpotImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet shadowSpotSampleImageWrite(m_allRTImageSampleDescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shadowSpotSampleImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo rtAOSampleImageInfo(m_rtAOImageSamplers.at(i), m_rtAOImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet rtAOSampleImageWrite(m_allRTImageSampleDescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &rtAOSampleImageInfo, nullptr, nullptr);

				vk::DescriptorImageInfo rtReflectionsSampleImageInfo(m_rtReflectionImageSamplers.at(i), m_rtReflectionImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
				vk::WriteDescriptorSet rtReflectionsSampleImageWrite(m_allRTImageSampleDescriptorSets.at(i), 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &rtReflectionsSampleImageInfo, nullptr, nullptr);
                
                vk::DescriptorImageInfo rtReflectionsSampleLowResImageInfo(m_rtReflectionLowResImageSamplers.at(i), m_rtReflectionLowResImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet rtReflectionsSampleLowResImageWrite(m_allRTImageSampleDescriptorSets.at(i), 5, 0, 1, vk::DescriptorType::eCombinedImageSampler, &rtReflectionsSampleLowResImageInfo, nullptr, nullptr);

                // store shadow descriptor set
                vk::DescriptorImageInfo shadowDirStoreImageInfo(nullptr, m_rtSoftShadowDirectionalImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet shadowDirStoreImageWrite(m_shadowImageStoreDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eStorageImage, &shadowDirStoreImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo shadowPointStoreImageInfo(nullptr, m_rtSoftShadowPointImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet shadowPointStoreImageWrite(m_shadowImageStoreDescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eStorageImage, &shadowPointStoreImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo shadowSpotStoreImageInfo(nullptr, m_rtSoftShadowSpotImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet shadowSpotStoreImageWrite(m_shadowImageStoreDescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eStorageImage, &shadowSpotStoreImageInfo, nullptr, nullptr);

                // store ao desc set
                vk::DescriptorImageInfo rtaotStoreImageInfo(nullptr, m_rtAOImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet rtaoStoreImageWrite(m_rtAOImageStoreDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eStorageImage, &rtaotStoreImageInfo, nullptr, nullptr);



                std::array descriptorWrites = {
                    shadowDirSampleImageWrite,
                    shadowPointSampleImageWrite,
                    shadowSpotSampleImageWrite,
                    rtAOSampleImageWrite,
                    shadowDirStoreImageWrite,
                    shadowPointStoreImageWrite,
                    shadowSpotStoreImageWrite,
                    rtaoStoreImageWrite,
					rtReflectionsSampleImageWrite,
                    rtReflectionsSampleLowResImageWrite
                };

                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        }

        void createRandomImage()
        {
            const auto ext = m_context.getSwapChainExtent();

            vk::DeviceSize imageSize = ext.width * ext.height * 4;
            auto sizeInBytes = imageSize * sizeof(uint32_t);
            std::vector<uint32_t> seedData(imageSize);
            std::random_device rd;
            std::mt19937 mt(rd());
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);

            std::for_each(std::execution::par, seedData.begin(), seedData.end(), [&seedData, &dist, &mt](auto &n)
            {
                n = (128 + static_cast<int32_t>(dist(mt) * seedData.size()));
            });

            auto stagingBuffer = createBuffer(sizeInBytes, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eExclusive, VMA_ALLOCATION_CREATE_MAPPED_BIT);
            memcpy(stagingBuffer.m_BufferAllocInfo.pMappedData, seedData.data(), sizeInBytes);

            auto cmdBuf = beginSingleTimeCommands(m_commandPool);

            // multi-buffered random image
            for(size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                using us = vk::ImageUsageFlagBits;
                m_randomImageInfos.push_back(createImage(ext.width, ext.height, 1, vk::Format::eR32G32B32A32Uint, vk::ImageTiling::eOptimal,
                    us::eTransferDst | us::eStorage, VMA_MEMORY_USAGE_GPU_ONLY));


                // transition image to transfer
                vk::ImageMemoryBarrier barrierRandomToTransfer(
                    {}, vk::AccessFlagBits::eTransferWrite,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_randomImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                cmdBuf.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {}, { barrierRandomToTransfer }
                );

                // copy data to image
                vk::ImageSubresourceLayers subreslayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
                vk::BufferImageCopy region(0, 0, 0, subreslayers, { 0, 0, 0 }, { ext.width, ext.height, 1 });
                vk::BufferImageCopy regionLowRes(0, 0, 0, subreslayers, { 0, 0, 0 }, { ext.width / 2, ext.height / 2, 1 });

                cmdBuf.copyBufferToImage(stagingBuffer.m_Buffer, m_randomImageInfos.at(i).m_Image, vk::ImageLayout::eTransferDstOptimal, region);

                // transition image to load/store
                vk::ImageMemoryBarrier barrierRandomFromTransferToStore(
                    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_randomImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );


                cmdBuf.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                    {}, {}, {}, { barrierRandomFromTransferToStore }
                );

                // create view
                const vk::ImageViewCreateInfo randomViewInfo({},
                    m_randomImageInfos.at(i).m_Image,
                    vk::ImageViewType::e2D,
                    vk::Format::eR32G32B32A32Uint,
                    {},
                    { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS });
                m_randomImageViews.push_back(m_context.getDevice().createImageView(randomViewInfo));
            }
            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);

            vmaDestroyBuffer(m_context.getAllocator(), stagingBuffer.m_Buffer, stagingBuffer.m_BufferAllocation);

            m_sampleCounts = std::vector<int32_t>(m_swapChainFramebuffers.size(), 0);
            std::vector<RTperFrameInfoCombined> initdata(1);
            for(size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
                m_rtPerFrameInfoBufferInfos.push_back(fillBufferTroughStagedTransfer(initdata, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst));
        }


        void createAccelerationStructure()
        {
            //TODO for this function:
            //	* use less for-loops and indices, some can be merged
            //	* support using transforms (?)

            //// Helper Buffers for Ray Tracing

            // Offset Buffer

            struct OffsetInfo
            {
                int m_vbOffset = 0;
                int m_ibOffset = 0;
                int m_baseColorTextureID = -1;
                int m_metallicRoughnessTextureID = -1;
            };

            std::vector<OffsetInfo> offsetInfos;
            int32_t indexOffset0 = 0;
            int j = 0;
            for (const PerMeshInfoPBR& meshInfo : m_scene.getDrawCommandData())
            {
                offsetInfos.push_back(OffsetInfo{ meshInfo.vertexOffset, indexOffset0, meshInfo.texIndexBaseColor, meshInfo.texIndexMetallicRoughness });
                indexOffset0 += meshInfo.indexCount;

                j++;
            }
            m_offsetBufferInfo = fillBufferTroughStagedTransfer(offsetInfos, vk::BufferUsageFlagBits::eStorageBuffer);

            std::vector<vk::GeometryNV> geometryVec;

            // TODO 1 Mesh = 1 BLAS + GeometryInstance w/ ModelMatrix as Transform


            //std::vector<glm::mat4x3> transforms;
            //for (const auto& modelMatrix : m_scene.getModelMatrices())
            //    transforms.emplace_back(toRowMajor4x3(modelMatrix));

            //m_transformBufferInfo = fillBufferTroughStagedTransfer(transforms, vk::BufferUsageFlagBits::eRayTracingNV);

            // currently: 1 geometry = 1 mesh
            size_t c = 0;
            uint64_t indexOffset = 0;
            for (const PerMeshInfoPBR& meshInfo : m_scene.getDrawCommandData())
            {

                uint32_t vertexCount = 0;

                //todo check this calculation
                if (c < m_scene.getDrawCommandData().size() - 1)
                    vertexCount = m_scene.getDrawCommandData().at(c + 1).vertexOffset - meshInfo.vertexOffset;
                else
                    vertexCount = static_cast<uint32_t>(m_scene.getVertices().size()) - meshInfo.vertexOffset;

                vk::GeometryTrianglesNV triangles;
                triangles.vertexData = m_vertexBufferInfo.m_Buffer;
                triangles.vertexOffset = meshInfo.vertexOffset * sizeof(VertexPosUvNormal);
                triangles.vertexCount = vertexCount;
                triangles.vertexStride = sizeof(VertexPosUvNormal);
                triangles.vertexFormat = VertexPosUvNormal::getAttributeDescriptions().at(0).format;
                triangles.indexData = m_indexBufferInfo.m_Buffer;
                triangles.indexOffset = indexOffset * sizeof(std::decay_t<decltype(m_scene.getIndices())>::value_type);
                triangles.indexCount = meshInfo.indexCount;
                triangles.indexType = vk::IndexType::eUint32;
                triangles.transformData = nullptr;// m_transformBufferInfo.m_Buffer;
                triangles.transformOffset = 0;// c * sizeof(glm::mat4x3);

                indexOffset += meshInfo.indexCount;

                vk::GeometryDataNV geoData(triangles, {});
                vk::GeometryNV geom(vk::GeometryTypeNV::eTriangles, geoData, vk::GeometryFlagBitsNV::eOpaque);

                geometryVec.push_back(geom);
                c++;
            }

            auto createActualAcc = [&]
            (vk::AccelerationStructureTypeNV type, uint32_t geometryCount, vk::GeometryNV* geometries, uint32_t instanceCount, vk::BuildAccelerationStructureFlagsNV flags) -> ASInfo
            {
                auto findMemoryType = [&](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t
                {
                    VkPhysicalDeviceMemoryProperties memProperties;
                    vkGetPhysicalDeviceMemoryProperties(m_context.getPhysicalDevice(), &memProperties);

                    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
                    {
                        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                        {
                            return i;
                        }
                    }

                    throw std::runtime_error("failed to find suitable memory type!");
                };

                ASInfo returnInfo;
                vk::AccelerationStructureInfoNV asInfo(type, flags, instanceCount, geometryCount, geometries);
                vk::AccelerationStructureCreateInfoNV accStrucInfo(0, asInfo);
                returnInfo.m_AS = m_context.getDevice().createAccelerationStructureNV(accStrucInfo);

                vk::AccelerationStructureMemoryRequirementsInfoNV memReqAS(vk::AccelerationStructureMemoryRequirementsTypeNV::eObject, returnInfo.m_AS);
                vk::MemoryRequirements2 memReqs = m_context.getDevice().getAccelerationStructureMemoryRequirementsNV(memReqAS);

                VmaAllocationCreateInfo allocInfo = {};
                allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
                allocInfo.memoryTypeBits = findMemoryType(memReqs.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                vmaAllocateMemory(m_context.getAllocator(),
                    reinterpret_cast<VkMemoryRequirements*>(&memReqs.memoryRequirements),
                    &allocInfo, &returnInfo.m_BufferAllocation, &returnInfo.m_BufferAllocInfo);

                //todo cleanup: destroy AS, free memory

                vk::BindAccelerationStructureMemoryInfoNV bindInfo(returnInfo.m_AS, returnInfo.m_BufferAllocInfo.deviceMemory, returnInfo.m_BufferAllocInfo.offset, 0, nullptr);
                m_context.getDevice().bindAccelerationStructureMemoryNV(bindInfo);

                return returnInfo;
            };
            using basf = vk::BuildAccelerationStructureFlagBitsNV;

            for (auto& geometry : geometryVec)
                m_bottomASs.push_back(createActualAcc(vk::AccelerationStructureTypeNV::eBottomLevel, 1, &geometry, 0, basf::ePreferFastTrace));


            std::vector<GeometryInstance> instances;

            int count = 0;
            for (const auto& modelMatrix : m_scene.getModelMatrices())
            {
                GeometryInstance instance = {};
                auto transform = toRowMajor4x3(modelMatrix);
                memcpy(instance.transform, glm::value_ptr(transform), sizeof(instance.transform));
                instance.instanceId = count;
                instance.mask = 0xff;
                instance.instanceOffset = 0;
                instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;

                const auto res = m_context.getDevice().getAccelerationStructureHandleNV(m_bottomASs.at(count).m_AS, sizeof(uint64_t), &instance.accelerationStructureHandle);
                if (res != vk::Result::eSuccess) throw std::runtime_error("AS Handle could not be retrieved");

                instances.push_back(instance);
                count++;
            }

            // todo this buffer is gpu only. maybe change this to make it host visible & coherent like it is in the examples.
            // probaby wont be needed if the instanced is not transformed later on
            m_instanceBufferInfo = fillBufferTroughStagedTransferForComputeQueue(instances, vk::BufferUsageFlagBits::eRayTracingNV);


            m_topAS = createActualAcc(vk::AccelerationStructureTypeNV::eTopLevel, 0, nullptr, 1, basf::ePreferFastTrace | basf::eAllowUpdate);

            auto GetScratchBufferSize = [&](vk::AccelerationStructureNV handle)
            {
                vk::AccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo(vk::AccelerationStructureMemoryRequirementsTypeNV::eBuildScratch, handle);

                vk::MemoryRequirements2 memoryRequirements = m_context.getDevice().getAccelerationStructureMemoryRequirementsNV(memoryRequirementsInfo);

                VkDeviceSize result = memoryRequirements.memoryRequirements.size;
                return result;
            };

            vk::DeviceSize maxBLASSize = 0;
            for (const auto& blas : m_bottomASs)
            {
                maxBLASSize = std::max(GetScratchBufferSize(blas.m_AS), maxBLASSize);
            }

            //VkDeviceSize bottomAccelerationStructureBufferSize = GetScratchBufferSize(m_bottomAS.m_AS);
            VkDeviceSize topAccelerationStructureBufferSize = GetScratchBufferSize(m_topAS.m_AS);
            VkDeviceSize scratchBufferSize = std::max(maxBLASSize, topAccelerationStructureBufferSize);

            vg::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
            std::array queueFamilyIndices = { indices.graphicsFamily.value(), indices.computeFamily.value() };
            vk::BufferCreateInfo bufferCreateInfo({}, scratchBufferSize, vk::BufferUsageFlagBits::eRayTracingNV, vk::SharingMode::eConcurrent, static_cast<uint32_t>(queueFamilyIndices.size()), queueFamilyIndices.data());
            allocBufferVma(m_scratchBuffer, bufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);

            //m_scratchBuffer = createBuffer(scratchBufferSize, vk::BufferUsageFlagBits::eRayTracingNV, VMA_MEMORY_USAGE_GPU_ONLY);

            //// compute shader test
            //const auto csc = Utility::readFile("combined/test.comp.spv");
            //const auto csm = m_context.createShaderModule(csc);
            //vk::PipelineShaderStageCreateInfo pssci({}, vk::ShaderStageFlagBits::eCompute, csm, "main");
            //auto pl = m_context.getDevice().createPipelineLayoutUnique({});
            //vk::ComputePipelineCreateInfo cpci({}, pssci, pl.get());
            //auto cp = m_context.getDevice().createComputePipelineUnique(nullptr, cpci);

            //auto cmdBufComp = beginSingleTimeCommands(m_computeCommandPool);

            //cmdBufComp.bindPipeline(vk::PipelineBindPoint::eCompute, cp.get());
            //cmdBufComp.dispatch(1, 1, 1);


            //endSingleTimeCommands(cmdBufComp, m_context.getComputeQueue(), m_computeCommandPool);
            //m_context.getDevice().waitIdle();

            auto cmdBuf = beginSingleTimeCommands(m_commandPool);
            m_timerManager.writeTimestampStart("AS Build", cmdBuf, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, 0);

#undef MemoryBarrier
            vk::MemoryBarrier memoryBarrier(
                vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV,
                vk::AccessFlagBits::eAccelerationStructureReadNV
            );
#define MemoryBarrier __faststorefence

            //todo remove this when the SDK update happened
            auto OwnCmdBuildAccelerationStructureNV = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(m_context.getDevice(), "vkCmdBuildAccelerationStructureNV"));

            for (size_t i = 0; i < geometryVec.size(); i++)
            {
                vk::AccelerationStructureInfoNV asInfoBot(vk::AccelerationStructureTypeNV::eBottomLevel, basf::ePreferFastTrace, 0, 1, &geometryVec.at(i));
                OwnCmdBuildAccelerationStructureNV(cmdBuf, reinterpret_cast<VkAccelerationStructureInfoNV*>(&asInfoBot), nullptr, 0, VK_FALSE, m_bottomASs.at(i).m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
                //cmdBuf.buildAccelerationStructureNV(asInfoBot, nullptr, 0, VK_FALSE, m_bottomAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
                cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, {}, memoryBarrier, nullptr, nullptr);

            }

            vk::AccelerationStructureInfoNV asInfoTop(vk::AccelerationStructureTypeNV::eTopLevel, basf::ePreferFastTrace | basf::eAllowUpdate, static_cast<uint32_t>(instances.size()), 0, nullptr);
            OwnCmdBuildAccelerationStructureNV(cmdBuf, reinterpret_cast<VkAccelerationStructureInfoNV*>(&asInfoTop), m_instanceBufferInfo.m_Buffer, 0, VK_FALSE, m_topAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
            //cmdBuf.buildAccelerationStructureNV(asInfoTop, m_instanceBufferInfo.m_Buffer, 0, VK_FALSE, m_topAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
            cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eRayTracingShaderNV, {}, memoryBarrier, nullptr, nullptr);

            m_timerManager.writeTimestampStop("AS Build", cmdBuf, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, 0);
            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);

            m_context.getDevice().waitIdle();

            m_timerManager.querySpecificTimerResults("AS Build", 0);
            auto timeDiffs = m_timerManager.getTimer("AS Build").getTimeDiffs();
            m_context.getLogger()->info("Acceleration Structure Build took {} ms for {} meshes, {} triangles", timeDiffs.front(), instances.size(), m_scene.getIndices().size() / 3);
            //m_context.getLogger()->info("Flags: {}, {}", vk::to_string(basf::ePreferFastTrace), vk::to_string(basf::eAllowUpdate));

            m_timerManager.eraseTimer("AS Build");
        }

        void createRTSoftShadowsPipeline()
        {
            //// 1. DSL

            // AS
            vk::DescriptorSetLayoutBinding asLB(0, vk::DescriptorType::eAccelerationStructureNV, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            // Image Load/Store for output
            vk::DescriptorSetLayoutBinding gbufferPos(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding randomImageLB(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding rtPerFrame(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);

            std::array bindings = { asLB, gbufferPos, randomImageLB, rtPerFrame };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_rtSoftShadowsDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            //// 2. Create Pipeline

            const auto rgenShaderCode = Utility::readFile("combined/softshadowPBR.rgen.spv");
            const auto rgenShaderModule = m_context.createShaderModule(rgenShaderCode);

            //const auto ahitShaderCode = Utility::readFile("combined/softshadow.rahit.spv");
            //const auto ahitShaderModule = m_context.createShaderModule(ahitShaderCode);

            const auto chitShaderCode = Utility::readFile("combined/softshadow.rchit.spv");
            const auto chitShaderModule = m_context.createShaderModule(chitShaderCode);

            const auto missShaderCode = Utility::readFile("combined/softshadow.rmiss.spv");
            const auto missShaderModule = m_context.createShaderModule(missShaderCode);


            std::array rtShaderStageInfos = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eRaygenNV, rgenShaderModule, "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitNV, chitShaderModule, "main"),
                //vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eAnyHitNV, ahitShaderModule, "main"),

                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissNV, missShaderModule, "main")
            };

            std::array dss = { m_rtSoftShadowsDescriptorSetLayout, m_lightDescriptorSetLayout, m_shadowImageStoreDescriptorSetLayout };
            vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo({}, static_cast<uint32_t>(dss.size()), dss.data());

            m_rtSoftShadowsPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutCreateInfo);

            std::array shaderGroups = {
                // group 0: raygen
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 0, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                // group 1: any hit
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, 1, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                //vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, 2, VK_SHADER_UNUSED_NV},
                // group 2: miss
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 2, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV}
            };

            vk::RayTracingPipelineCreateInfoNV rayPipelineInfo({},
                static_cast<uint32_t>(rtShaderStageInfos.size()), rtShaderStageInfos.data(),
                static_cast<uint32_t>(shaderGroups.size()), shaderGroups.data(),
                1,
                m_rtSoftShadowsPipelineLayout,
                nullptr, 0
            );

            m_rtSoftShadowsPipeline = m_context.getDevice().createRayTracingPipelinesNV(nullptr, rayPipelineInfo).at(0);

            // destroy shader modules:
            m_context.getDevice().destroyShaderModule(rgenShaderModule);
            //m_context.getDevice().destroyShaderModule(ahitShaderModule);
            m_context.getDevice().destroyShaderModule(chitShaderModule);
            m_context.getDevice().destroyShaderModule(missShaderModule);

            //// 3. Create Shader Binding Table

            const uint32_t shaderBindingTableSize = m_context.getRaytracingProperties().shaderGroupHandleSize * rayPipelineInfo.groupCount;
            
            if(!m_rtSoftShadowSBTInfo.m_Buffer)
                m_rtSoftShadowSBTInfo = createBuffer(shaderBindingTableSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
            void* mappedData;
            vmaMapMemory(m_context.getAllocator(), m_rtSoftShadowSBTInfo.m_BufferAllocation, &mappedData);
            const auto res = m_context.getDevice().getRayTracingShaderGroupHandlesNV(m_rtSoftShadowsPipeline, 0, rayPipelineInfo.groupCount, shaderBindingTableSize, mappedData);
            if (res != vk::Result::eSuccess) throw std::runtime_error("Failed to retrieve Shader Group Handles");
            vmaUnmapMemory(m_context.getAllocator(), m_rtSoftShadowSBTInfo.m_BufferAllocation);

            //// 4. Create Descriptor Set

            // deviation from tutorial: I'm creating multiple descriptor sets, and binding the one with the current swap chain image

            if (m_rtSoftShadowsDescriptorSets.empty())
            {
                // create n descriptor sets
                std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_rtSoftShadowsDescriptorSetLayout);
                vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
                m_rtSoftShadowsDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);
            }



            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                vk::WriteDescriptorSetAccelerationStructureNV descriptorSetAccelerationStructureInfo(1, &m_topAS.m_AS);
                vk::WriteDescriptorSet accelerationStructureWrite(m_rtSoftShadowsDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eAccelerationStructureNV, nullptr, nullptr, nullptr);
                accelerationStructureWrite.setPNext(&descriptorSetAccelerationStructureInfo); // pNext is assigned here!!!

                vk::DescriptorImageInfo descriptorGBufferPosImageInfo(m_gbufferPositionSamplers.at(i), m_gbufferPositionImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet gbufferPosImageWrite(m_rtSoftShadowsDescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferPosImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo randomImageInfo(nullptr, m_randomImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet randomImageWrite(m_rtSoftShadowsDescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eStorageImage, &randomImageInfo, nullptr, nullptr);

                vk::DescriptorBufferInfo rtPerFrameInfo(m_rtPerFrameInfoBufferInfos.at(i).m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet  rtPerFrameWrite(m_rtSoftShadowsDescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &rtPerFrameInfo, nullptr);


                std::array descriptorWrites = { accelerationStructureWrite, gbufferPosImageWrite, randomImageWrite, rtPerFrameWrite };
                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        }

        void createRTAOPipeline()
        {
            //// 1. DSL
            // TODO streamline DSs: 1DS for Gbuffer, 1 for what all RT passes use, 1 per RT pass with anything else
            // AS
            vk::DescriptorSetLayoutBinding asLB(0, vk::DescriptorType::eAccelerationStructureNV, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            // Image Load/Store for output
            vk::DescriptorSetLayoutBinding gbufferPos(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding gbufferNormal(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding randomImageLB(3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding rtPerFrame(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);


            std::array bindings = { asLB, gbufferPos, gbufferNormal, randomImageLB, rtPerFrame };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_rtAODescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            //// 2. Create Pipeline

            const auto rgenShaderCode = Utility::readFile("combined/rtao.rgen.spv");
            const auto rgenShaderModule = m_context.createShaderModule(rgenShaderCode);

            //const auto ahitShaderCode = Utility::readFile("combined/rtao.rahit.spv");
            //const auto ahitShaderModule = m_context.createShaderModule(ahitShaderCode);

            const auto chitShaderCode = Utility::readFile("combined/rtao.rchit.spv");
            const auto chitShaderModule = m_context.createShaderModule(chitShaderCode);

            const auto missShaderCode = Utility::readFile("combined/rtao.rmiss.spv");
            const auto missShaderModule = m_context.createShaderModule(missShaderCode);


            std::array rtShaderStageInfos = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eRaygenNV, rgenShaderModule, "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitNV, chitShaderModule, "main"),
                //vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eAnyHitNV, ahitShaderModule, "main"),

                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissNV, missShaderModule, "main")
            };

            std::array dss = { m_rtAODescriptorSetLayout, m_rtAOImageStoreDescriptorSetLayout };
            vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo({}, static_cast<uint32_t>(dss.size()), dss.data());

            m_rtAOPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutCreateInfo);

            std::array shaderGroups = {
                // group 0: raygen
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 0, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                // group 1: any hit
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, 1, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                //vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, 2, VK_SHADER_UNUSED_NV},
                // group 2: miss
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 2, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV}
            };

            vk::RayTracingPipelineCreateInfoNV rayPipelineInfo({},
                static_cast<uint32_t>(rtShaderStageInfos.size()), rtShaderStageInfos.data(),
                static_cast<uint32_t>(shaderGroups.size()), shaderGroups.data(),
                1,
                m_rtAOPipelineLayout,
                nullptr, 0
            );

            m_rtAOPipeline = m_context.getDevice().createRayTracingPipelinesNV(nullptr, rayPipelineInfo).at(0);

            // destroy shader modules:
            m_context.getDevice().destroyShaderModule(rgenShaderModule);
            //m_context.getDevice().destroyShaderModule(ahitShaderModule);
            m_context.getDevice().destroyShaderModule(chitShaderModule);
            m_context.getDevice().destroyShaderModule(missShaderModule);

            //// 3. Create Shader Binding Table

            const uint32_t shaderBindingTableSize = m_context.getRaytracingProperties().shaderGroupHandleSize * rayPipelineInfo.groupCount;

            if (!m_rtAOSBTInfo.m_Buffer)
                m_rtAOSBTInfo = createBuffer(shaderBindingTableSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
            void* mappedData;
            vmaMapMemory(m_context.getAllocator(), m_rtAOSBTInfo.m_BufferAllocation, &mappedData);
            const auto res = m_context.getDevice().getRayTracingShaderGroupHandlesNV(m_rtAOPipeline, 0, rayPipelineInfo.groupCount, shaderBindingTableSize, mappedData);
            if (res != vk::Result::eSuccess) throw std::runtime_error("Failed to retrieve Shader Group Handles");
            vmaUnmapMemory(m_context.getAllocator(), m_rtAOSBTInfo.m_BufferAllocation);

            //// 4. Create Descriptor Set

            // deviation from tutorial: I'm creating multiple descriptor sets, and binding the one with the current swap chain image

            if (m_rtAODescriptorSets.empty())
            {
                // create n descriptor sets
                std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_rtAODescriptorSetLayout);
                vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
                m_rtAODescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);
            }



            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                vk::WriteDescriptorSetAccelerationStructureNV descriptorSetAccelerationStructureInfo(1, &m_topAS.m_AS);
                vk::WriteDescriptorSet accelerationStructureWrite(m_rtAODescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eAccelerationStructureNV, nullptr, nullptr, nullptr);
                accelerationStructureWrite.setPNext(&descriptorSetAccelerationStructureInfo); // pNext is assigned here!!!

                vk::DescriptorImageInfo descriptorGBufferPosImageInfo(m_gbufferPositionSamplers.at(i), m_gbufferPositionImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet gbufferPosImageWrite(m_rtAODescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferPosImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo descriptorGBufferNormalImageInfo(m_gbufferNormalSamplers.at(i), m_gbufferNormalImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet gbufferNormalImageWrite(m_rtAODescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferNormalImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo randomImageInfo(nullptr, m_randomImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet randomImageWrite(m_rtAODescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eStorageImage, &randomImageInfo, nullptr, nullptr);

                vk::DescriptorBufferInfo rtPerFrameInfo(m_rtPerFrameInfoBufferInfos.at(i).m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet  rtPerFrameWrite(m_rtAODescriptorSets.at(i), 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &rtPerFrameInfo, nullptr);


                std::array descriptorWrites = { accelerationStructureWrite, gbufferPosImageWrite, gbufferNormalImageWrite, randomImageWrite, rtPerFrameWrite };
                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        }

		void createRTReflectionPipeline()
        {
			//// 1. DSL
			 // TODO streamline DSs: 1DS for Gbuffer, 1 for what all RT passes use, 1 per RT pass with anything else
			 // AS
			vk::DescriptorSetLayoutBinding asLB(0, vk::DescriptorType::eAccelerationStructureNV, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			// GBuffer
			vk::DescriptorSetLayoutBinding gbufferPos(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
			vk::DescriptorSetLayoutBinding gbufferNormal(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding gbufferUV(12, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);

            // add. info
        	vk::DescriptorSetLayoutBinding randomImageLB(3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);

            vk::DescriptorSetLayoutBinding rtPerFrame(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			//output image
			vk::DescriptorSetLayoutBinding reflectionImageLB(5, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            vk::DescriptorSetLayoutBinding reflectionLowResImageLB(13, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);

            // info for shading
			vk::DescriptorSetLayoutBinding vertexBufferLB(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			vk::DescriptorSetLayoutBinding indexBufferLB(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			vk::DescriptorSetLayoutBinding offsetBufferLB(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			
			vk::DescriptorSetLayoutBinding materialBufferLB(9, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
			vk::DescriptorSetLayoutBinding indirectDrawBufferLB(10, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);

        	vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(11, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImages.size()), vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV, nullptr);

			std::array bindings = { asLB, gbufferPos, gbufferNormal,gbufferUV, randomImageLB, rtPerFrame,reflectionImageLB,
			vertexBufferLB,indexBufferLB, offsetBufferLB, materialBufferLB, indirectDrawBufferLB, allTexturesLayoutBinding, reflectionLowResImageLB };

			vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

			m_rtReflectionsDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

			//// 2. Create Pipeline

			const auto rgenShaderCode = Utility::readFile("combined/rtreflectionsPBR.rgen.spv");
			const auto rgenShaderModule = m_context.createShaderModule(rgenShaderCode);
			
			const auto chitShaderCode = Utility::readFile("combined/rtreflectionsPBR.rchit.spv");
			const auto chitShaderModule = m_context.createShaderModule(chitShaderCode);

			const auto missShaderCode = Utility::readFile("combined/rtreflections.rmiss.spv");
			const auto missShaderModule = m_context.createShaderModule(missShaderCode);

			const auto chitSecondaryShaderCode = Utility::readFile("combined/rtreflectionsSecondaryShadow.rchit.spv");
			const auto chitSecondaryShaderModule = m_context.createShaderModule(chitSecondaryShaderCode);

			const auto missSecondaryShaderCode = Utility::readFile("combined/rtreflectionsSecondaryShadow.rmiss.spv");
			const auto missSecondaryShaderModule = m_context.createShaderModule(missSecondaryShaderCode);


			std::array rtShaderStageInfos = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eRaygenNV, rgenShaderModule, "main"),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitNV, chitShaderModule, "main"),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitNV, chitSecondaryShaderModule, "main"),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissNV, missShaderModule, "main"),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissNV, missSecondaryShaderModule, "main")
			};

			std::array dss = { m_rtReflectionsDescriptorSetLayout, m_lightDescriptorSetLayout };
			vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo({}, static_cast<uint32_t>(dss.size()), dss.data());

			m_rtReflectionsPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutCreateInfo);

			std::array shaderGroups = {
				// group 0: raygen
				vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 0, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
				// group 1: closest hit (for reflections)
				vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, 1, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
				// group 2: closest hit (for secondary rays: shadows in reflection)
				vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, 2, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
				// group 3: miss (for reflection rays)
				vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 3, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
				// group 4: miss (for secondary rays: shadows in reflection)
				vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 4, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV}

			};

			vk::RayTracingPipelineCreateInfoNV rayPipelineInfo({},
				static_cast<uint32_t>(rtShaderStageInfos.size()), rtShaderStageInfos.data(),
				static_cast<uint32_t>(shaderGroups.size()), shaderGroups.data(),
				2,
				m_rtReflectionsPipelineLayout,
				nullptr, 0
			);

			m_rtReflectionsPipeline = m_context.getDevice().createRayTracingPipelinesNV(nullptr, rayPipelineInfo).at(0);

			// destroy shader modules:
			m_context.getDevice().destroyShaderModule(rgenShaderModule);
			m_context.getDevice().destroyShaderModule(chitShaderModule);
			m_context.getDevice().destroyShaderModule(missShaderModule);
			m_context.getDevice().destroyShaderModule(chitSecondaryShaderModule);
			m_context.getDevice().destroyShaderModule(missSecondaryShaderModule);

			//// 3. Create Shader Binding Table

			const uint32_t shaderBindingTableSize = m_context.getRaytracingProperties().shaderGroupHandleSize * rayPipelineInfo.groupCount;

			if (!m_rtReflectionsSBTInfo.m_Buffer)
				m_rtReflectionsSBTInfo = createBuffer(shaderBindingTableSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
			void* mappedData;
			vmaMapMemory(m_context.getAllocator(), m_rtReflectionsSBTInfo.m_BufferAllocation, &mappedData);
			const auto res = m_context.getDevice().getRayTracingShaderGroupHandlesNV(m_rtReflectionsPipeline, 0, rayPipelineInfo.groupCount, shaderBindingTableSize, mappedData);
			if (res != vk::Result::eSuccess) throw std::runtime_error("Failed to retrieve Shader Group Handles");
			vmaUnmapMemory(m_context.getAllocator(), m_rtReflectionsSBTInfo.m_BufferAllocation);

			//// 4. Create Descriptor Set

			// deviation from tutorial: I'm creating multiple descriptor sets, and binding the one with the current swap chain image

			if (m_rtReflectionsDescriptorSets.empty())
			{
				// create n descriptor sets
				std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_rtReflectionsDescriptorSetLayout);
				vk::DescriptorSetAllocateInfo desSetAllocInfo(m_combinedDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
				m_rtReflectionsDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);
			}


			std::vector<vk::DescriptorImageInfo> allImageInfos;
			for (size_t i = 0; i < m_allImages.size(); i++)
			{
				allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
			}


			for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
			{
				vk::WriteDescriptorSetAccelerationStructureNV descriptorSetAccelerationStructureInfo(1, &m_topAS.m_AS);
				vk::WriteDescriptorSet accelerationStructureWrite(m_rtReflectionsDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eAccelerationStructureNV, nullptr, nullptr, nullptr);
				accelerationStructureWrite.setPNext(&descriptorSetAccelerationStructureInfo); // pNext is assigned here!!!

				vk::DescriptorImageInfo descriptorGBufferPosImageInfo(m_gbufferPositionSamplers.at(i), m_gbufferPositionImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
				vk::WriteDescriptorSet gbufferPosImageWrite(m_rtReflectionsDescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferPosImageInfo, nullptr, nullptr);

				vk::DescriptorImageInfo descriptorGBufferNormalImageInfo(m_gbufferNormalSamplers.at(i), m_gbufferNormalImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
				vk::WriteDescriptorSet gbufferNormalImageWrite(m_rtReflectionsDescriptorSets.at(i), 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferNormalImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo descriptorGBufferUVImageInfo(m_gbufferUVSamplers.at(i), m_gbufferUVImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet gbufferUVImageWrite(m_rtReflectionsDescriptorSets.at(i), 12, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorGBufferUVImageInfo, nullptr, nullptr);

				
				vk::DescriptorImageInfo randomImageInfo(nullptr, m_randomImageViews.at(i), vk::ImageLayout::eGeneral);
				vk::WriteDescriptorSet randomImageWrite(m_rtReflectionsDescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eStorageImage, &randomImageInfo, nullptr, nullptr);

				vk::DescriptorBufferInfo rtPerFrameInfo(m_rtPerFrameInfoBufferInfos.at(i).m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet  rtPerFrameWrite(m_rtReflectionsDescriptorSets.at(i), 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &rtPerFrameInfo, nullptr);
				
				vk::DescriptorImageInfo reflectionImageInfo(nullptr, m_rtReflectionImageViews.at(i), vk::ImageLayout::eGeneral);
				vk::WriteDescriptorSet reflectionImageWrite(m_rtReflectionsDescriptorSets.at(i), 5, 0, 1, vk::DescriptorType::eStorageImage, &reflectionImageInfo, nullptr, nullptr);

                vk::DescriptorImageInfo reflectionLowResImageInfo(nullptr, m_rtReflectionLowResImageViews.at(i), vk::ImageLayout::eGeneral);
                vk::WriteDescriptorSet reflectionLowResImageWrite(m_rtReflectionsDescriptorSets.at(i), 13, 0, 1, vk::DescriptorType::eStorageImage, &reflectionLowResImageInfo, nullptr, nullptr);


				vk::DescriptorBufferInfo vbInfo(m_vertexBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet descWriteVertexBuffer(m_rtReflectionsDescriptorSets.at(i), 6, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &vbInfo, nullptr);
				vk::DescriptorBufferInfo ibInfo(m_indexBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet descWriteIndexBuffer(m_rtReflectionsDescriptorSets.at(i), 7, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &ibInfo, nullptr);
				vk::DescriptorBufferInfo obInfo(m_offsetBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet descWriteOffsetBuffer(m_rtReflectionsDescriptorSets.at(i), 8, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &obInfo, nullptr);
				
				vk::DescriptorBufferInfo matBufferInfo(m_materialBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet descWriteMaterialBuffer(m_rtReflectionsDescriptorSets.at(i), 9, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &matBufferInfo, nullptr);

				vk::DescriptorBufferInfo indirectBufferInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet descWriteIndirectBuffer(m_rtReflectionsDescriptorSets.at(i), 10, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indirectBufferInfo, nullptr);


				vk::WriteDescriptorSet descWriteAllImages(m_rtReflectionsDescriptorSets.at(i), 11, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

				std::array descriptorWrites = { accelerationStructureWrite,gbufferPosImageWrite, gbufferNormalImageWrite,gbufferUVImageWrite, randomImageWrite,
					rtPerFrameWrite , reflectionImageWrite, descWriteVertexBuffer, descWriteIndexBuffer, descWriteOffsetBuffer,
					descWriteMaterialBuffer, descWriteIndirectBuffer, descWriteAllImages, reflectionLowResImageWrite };
				m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
			}
        }

        void createVertexBuffer()
        {
            m_vertexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getVertices(), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createIndexBuffer()
        {
            m_indexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getIndices(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createIndirectDrawBuffer()
        {
            m_indirectDrawBufferInfo = fillBufferTroughStagedTransfer(m_scene.getDrawCommandData(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createPerGeometryBuffers()
        {
            m_modelMatrixBufferInfo = fillBufferTroughStagedTransfer(m_scene.getModelMatrices(), vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createMaterialBuffer()
        {
            m_materialBufferInfo = fillBufferTroughStagedTransfer(m_scene.getMaterials(), vk::BufferUsageFlagBits::eStorageBuffer);
        }

        void createPerFrameInformation()
        {
            m_camera = Pilotview(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height);
            m_camera.setSensitivity(0.1f);
            m_projection = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10000.0f);
            m_projection[1][1] *= -1;

            m_camera.update(m_context.getWindow());
            m_camera.resetChangeFlag();

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

            vk::CommandBufferAllocateInfo cmdAllocInfoCompute(m_computeCommandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_computeCommandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfoCompute);
            m_computeFinishedFences.resize(m_swapChainFramebuffers.size());
            vk::FenceCreateInfo fenceInfo;
            for (auto & fence : m_computeFinishedFences)
                fence = m_context.getDevice().createFence(fenceInfo);


            // static secondary buffers (containing draw calls), never change
            vk::CommandBufferAllocateInfo secondaryCmdAllocInfo(m_commandPool, vk::CommandBufferLevel::eSecondary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_gbufferSecondaryCommandBuffers            = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
            m_fullscreenLightingSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
            m_rtSoftShadowsSecondaryCommandBuffers      = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
            m_rtAOSecondaryCommandBuffers               = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
			m_rtReflectionsSecondaryCommandBuffers		= m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);
            m_rtReflectionsLowResSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);


            // dynamic secondary buffers (containing per-frame information), getting re-recorded if necessary
            m_perFrameSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);          

            // fill static command buffers:
            for (size_t i = 0; i < m_gbufferSecondaryCommandBuffers.size(); i++)
            {
                //// gbuffer pass command buffers
                vk::CommandBufferInheritanceInfo inheritanceInfo(m_gbufferRenderpass, 0, m_gbufferFramebuffers.at(i), 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);
                m_gbufferSecondaryCommandBuffers.at(i).begin(beginInfo);
                m_timerManager.writeTimestampStart("1 G-Buffer", m_gbufferSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eAllGraphics, i);

                m_gbufferSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_gbufferGraphicsPipeline);

                m_gbufferSecondaryCommandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
                m_gbufferSecondaryCommandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint32);

                m_gbufferSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_gbufferPipelineLayout, 0, 1, &m_gbufferDescriptorSets.at(0), 0, nullptr);

                m_gbufferSecondaryCommandBuffers.at(i).drawIndexedIndirect(m_indirectDrawBufferInfo.m_Buffer, 0, static_cast<uint32_t>(m_scene.getDrawCommandData().size()),
                    sizeof(std::decay_t<decltype(*m_scene.getDrawCommandData().data())>));
                
                m_timerManager.writeTimestampStop("1 G-Buffer", m_gbufferSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eAllGraphics, i);
                m_gbufferSecondaryCommandBuffers.at(i).end();

                //TODO synchronization for g-buffer resources should be done "implicitly" by renderpasses. check this


                //// fullscreen lighting pass command buffers
                vk::CommandBufferInheritanceInfo inheritanceInfo2(m_fullscreenLightingRenderpass, 0, m_swapChainFramebuffers.at(i), 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo2(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo2);
                m_fullscreenLightingSecondaryCommandBuffers.at(i).begin(beginInfo2);
                m_timerManager.writeTimestampStart("5 Fullscreen Lighting", m_fullscreenLightingSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eAllGraphics, i);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_fullscreenLightingPipeline);

                // important: bind the descriptor set corresponding to the correct multi-buffered gbuffer resources
                std::array descSets = { m_fullScreenLightingDescriptorSets.at(i), m_lightDescriptorSet, m_allRTImageSampleDescriptorSets.at(i) };
                m_fullscreenLightingSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_fullscreenLightingPipelineLayout,
                    0, static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).draw(3, 1, 0, 0);
                
                m_timerManager.writeTimestampStop("5 Fullscreen Lighting", m_fullscreenLightingSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eAllGraphics, i);

                m_fullscreenLightingSecondaryCommandBuffers.at(i).end();



                //// ray tracing (for shadows) command buffers
                vk::CommandBufferInheritanceInfo inheritanceInfo3(nullptr, 0, nullptr, 0, {}, {});
                vk::CommandBufferBeginInfo beginInfo3(vk::CommandBufferUsageFlagBits::eSimultaneousUse , &inheritanceInfo3);
                m_rtSoftShadowsSecondaryCommandBuffers.at(i).begin(beginInfo3);

                m_rtSoftShadowsSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eRayTracingNV, m_rtSoftShadowsPipeline);
                std::array dss = { m_rtSoftShadowsDescriptorSets.at(i), m_lightDescriptorSet, m_shadowImageStoreDescriptorSets.at(i) };
                m_rtSoftShadowsSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, m_rtSoftShadowsPipelineLayout,
                    0, static_cast<uint32_t>(dss.size()), dss.data(), 0, nullptr);

                // transition gbuffer images to read it in RT //TODO transition back?
                vk::ImageMemoryBarrier barrierGBposTORT(
                    vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_gbufferPositionImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                vk::ImageMemoryBarrier barrierGBnormalTORT = barrierGBposTORT;
                barrierGBnormalTORT.setImage(m_gbufferNormalImageInfos.at(i).m_Image);
                barrierGBnormalTORT.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));

                vk::ImageMemoryBarrier barrierGBuvTORT = barrierGBposTORT;
                barrierGBuvTORT.setImage(m_gbufferUVImageInfos.at(i).m_Image);


                std::array gBufferBarriers = { barrierGBposTORT, barrierGBnormalTORT, barrierGBuvTORT };

                m_rtSoftShadowsSecondaryCommandBuffers.at(i).pipelineBarrier(
                    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                    vk::DependencyFlagBits::eByRegion, {}, {}, gBufferBarriers
                );

                // transition shadow image to write to it in raygen shader
                vk::ImageMemoryBarrier barrierPointShadowTORT(
                    vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderWrite,
                    vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_rtSoftShadowPointImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                vk::ImageMemoryBarrier barrierSpotShadowTORT = barrierPointShadowTORT;
                barrierSpotShadowTORT.image = m_rtSoftShadowSpotImageInfos.at(i).m_Image;

                m_rtSoftShadowsSecondaryCommandBuffers.at(i).pipelineBarrier(
                    vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                    vk::DependencyFlagBits::eByRegion, {}, {}, { barrierPointShadowTORT, barrierSpotShadowTORT }
                );
                m_timerManager.writeTimestampStart("2 Ray Traced Shadows", m_rtSoftShadowsSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eRayTracingShaderNV, i);


                auto OwnCmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(m_context.getDevice(), "vkCmdTraceRaysNV"));
                OwnCmdTraceRays(m_rtSoftShadowsSecondaryCommandBuffers.at(i),
                    m_rtSoftShadowSBTInfo.m_Buffer, 0, // raygen
                    m_rtSoftShadowSBTInfo.m_Buffer, 2 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // miss
                    m_rtSoftShadowSBTInfo.m_Buffer, 1 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // (any) hit
                    nullptr, 0, 0, // callable
                    m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height, 1
                );

                m_timerManager.writeTimestampStop("2 Ray Traced Shadows", m_rtSoftShadowsSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eRayTracingShaderNV, i);

                //vk::ImageMemoryBarrier barrierRandomImage(
                //    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                //    vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                //    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                //    m_randomImageInfos.at(i).m_Image,
                //    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                //);
                //m_rtSoftShadowsSecondaryCommandBuffers.at(i).pipelineBarrier(
                //    vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                //    vk::DependencyFlagBits::eByRegion, {}, {}, { barrierRandomImage }
                //);


                // transition image to read it in the fullscreen lighting shader
                vk::ImageMemoryBarrier barrierPointShadowTOFS(
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_rtSoftShadowPointImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                vk::ImageMemoryBarrier barrierSpotShadowTOFS = barrierPointShadowTOFS;
                barrierSpotShadowTOFS.image = m_rtSoftShadowSpotImageInfos.at(i).m_Image;

                m_rtSoftShadowsSecondaryCommandBuffers.at(i).pipelineBarrier(
                    vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::DependencyFlagBits::eByRegion, {}, {}, { barrierPointShadowTOFS , barrierSpotShadowTOFS }
                );

                m_rtSoftShadowsSecondaryCommandBuffers.at(i).end();


                //// AO Pass ////

                m_rtAOSecondaryCommandBuffers.at(i).begin(beginInfo3);
                m_timerManager.writeTimestampStart("3 Ray Traced Ambient Occlusion", m_rtAOSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eRayTracingShaderNV, i);

                m_rtAOSecondaryCommandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eRayTracingNV, m_rtAOPipeline);
                std::array dss2 = { m_rtAODescriptorSets.at(i), m_rtAOImageStoreDescriptorSets.at(i) };
                m_rtAOSecondaryCommandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, m_rtAOPipelineLayout,
                    0, static_cast<uint32_t>(dss2.size()), dss2.data(), 0, nullptr);

                // transition shadow image to write to it in raygen shader
                vk::ImageMemoryBarrier barrierAOTORT(
                    vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderWrite,
                    vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_rtAOImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                m_rtAOSecondaryCommandBuffers.at(i).pipelineBarrier(
                    vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                    vk::DependencyFlagBits::eByRegion, {}, {}, barrierAOTORT
                );

                OwnCmdTraceRays(m_rtAOSecondaryCommandBuffers.at(i),
                    m_rtAOSBTInfo.m_Buffer, 0, // raygen
                    m_rtAOSBTInfo.m_Buffer, 2 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // miss
                    m_rtAOSBTInfo.m_Buffer, 1 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // (any) hit
                    nullptr, 0, 0, // callable
                    m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height, 1
                );

                //m_rtAOSecondaryCommandBuffers.at(i).pipelineBarrier(
                //    vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                //    vk::DependencyFlagBits::eByRegion, {}, {}, { barrierRandomImage }
                //);

                // transition image to read it in the fullscreen lighting shader
                vk::ImageMemoryBarrier barrierAOTOFS(
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_rtAOImageInfos.at(i).m_Image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                );

                m_rtAOSecondaryCommandBuffers.at(i).pipelineBarrier(
                    vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::DependencyFlagBits::eByRegion, {}, {}, barrierAOTOFS
                );

                m_timerManager.writeTimestampStop("3 Ray Traced Ambient Occlusion", m_rtAOSecondaryCommandBuffers.at(i), vk::PipelineStageFlagBits::eRayTracingShaderNV, i);
                m_rtAOSecondaryCommandBuffers.at(i).end();

				///// REFLECTION PASS /////

                auto generateReflectionSecondaryCommandBuffer = [this, &OwnCmdTraceRays, &beginInfo3](const glm::ivec2& extent, vk::CommandBuffer& commandBuffer, const size_t i)
                {
                    commandBuffer.begin(beginInfo3);
                    m_timerManager.writeTimestampStart("4 Ray Traced Reflections", commandBuffer, vk::PipelineStageFlagBits::eRayTracingShaderNV, i);

                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingNV, m_rtReflectionsPipeline);
                    std::array dss3 = { m_rtReflectionsDescriptorSets.at(i), m_lightDescriptorSet };
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, m_rtReflectionsPipelineLayout,
                        0, static_cast<uint32_t>(dss3.size()), dss3.data(), 0, nullptr);

                    // transition shadow image to write to it in raygen shader
                    vk::ImageMemoryBarrier barrierReflTORT(
                        vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderWrite,
                        vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral,
                        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                        m_rtReflectionImageInfos.at(i).m_Image,
                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                    );
                    vk::ImageMemoryBarrier barrierLowResReflTORT = barrierReflTORT;
                    barrierLowResReflTORT.image = m_rtReflectionLowResImageInfos.at(i).m_Image;

                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                        vk::DependencyFlagBits::eByRegion, {}, {}, { barrierReflTORT, barrierLowResReflTORT }
                    );

                    OwnCmdTraceRays(commandBuffer,
                        m_rtReflectionsSBTInfo.m_Buffer, 0, // raygen
                        m_rtReflectionsSBTInfo.m_Buffer, 3 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // miss
                        m_rtReflectionsSBTInfo.m_Buffer, 1 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // closest hit
                        nullptr, 0, 0, // callable
                        extent.x, extent.y, 1
                    );

                    //m_rtReflectionsSecondaryCommandBuffers.at(i).pipelineBarrier(
                    //    vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                    //    vk::DependencyFlagBits::eByRegion, {}, {}, { barrierRandomImage }
                    //);

                    // transition image to read it in the fullscreen lighting shader
                    vk::ImageMemoryBarrier barrierReflTOFS(
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                        vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                        m_rtReflectionImageInfos.at(i).m_Image,
                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS)
                    );

                    vk::ImageMemoryBarrier barrierLowResReflTOFS = barrierReflTOFS;
                    barrierLowResReflTOFS.image = m_rtReflectionLowResImageInfos.at(i).m_Image;

                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eFragmentShader,
                        vk::DependencyFlagBits::eByRegion, {}, {}, { barrierReflTOFS, barrierLowResReflTOFS }
                    );

                    m_timerManager.writeTimestampStop("4 Ray Traced Reflections", commandBuffer, vk::PipelineStageFlagBits::eRayTracingShaderNV, i);
                    commandBuffer.end();
                };

                glm::ivec2 extent(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height);
                glm::ivec2 extentLowRes(m_context.getSwapChainExtent().width / 2, m_context.getSwapChainExtent().height / 2);

                generateReflectionSecondaryCommandBuffer(extent, m_rtReflectionsSecondaryCommandBuffers.at(i), i);
                generateReflectionSecondaryCommandBuffer(extentLowRes, m_rtReflectionsLowResSecondaryCommandBuffers.at(i), i);

            }
        }

        void recordPerFrameCommandBuffers(uint32_t currentImage) override
        {
            ////// Secondary Command Buffer with per-frame information (TODO: this can be done in a seperate thread)
            m_perFrameSecondaryCommandBuffers.at(currentImage).reset({});

            vk::CommandBufferInheritanceInfo inheritanceInfo(m_gbufferRenderpass, 0, m_gbufferFramebuffers.at(currentImage), 0, {}, {});
            vk::CommandBufferBeginInfo beginInfo1(vk::CommandBufferUsageFlagBits::eSimultaneousUse | vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);

            m_perFrameSecondaryCommandBuffers.at(currentImage).begin(beginInfo1);

            m_camera.update(m_context.getWindow()); // reset is later in this function

            m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_gbufferPipelineLayout, 
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(glm::mat4),
                glm::value_ptr(m_camera.getView()));

            m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_gbufferPipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                sizeof(glm::mat4), sizeof(glm::mat4),
                glm::value_ptr(m_projection));
            m_projectionChanged = false;

            glm::vec4 cameraPos(m_camera.getPosition(), 1.0f);
            m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_fullscreenLightingPipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                2*sizeof(glm::mat4), sizeof(glm::vec4),
                &cameraPos);

			m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_fullscreenLightingPipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				2 * sizeof(glm::mat4) + sizeof(glm::vec4), sizeof(float),
				&m_exposure);

            m_perFrameSecondaryCommandBuffers.at(currentImage).pushConstants(m_fullscreenLightingPipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                2 * sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(float), sizeof(int32_t),
                &m_useLowResReflections);

            m_perFrameSecondaryCommandBuffers.at(currentImage).end();


            ////// Primary Command Buffer (doesn't really change, but still needs to be re-recorded)
            m_commandBuffers.at(currentImage).reset({});

            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);
            m_commandBuffers.at(currentImage).begin(beginInfo);


            // update TLAS
            if(m_animate) //TODO async compute
                //TODO fix update because it needs ping-pong AS
            {
                vk::CommandBuffer cmdBufForASUpdate = m_useAsync ? m_computeCommandBuffers.at(currentImage) : m_commandBuffers.at(currentImage);

                m_timerManager.writeTimestampStart("0 AS Update", cmdBufForASUpdate, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, currentImage);

                //// Testing async compute with this pipeline
                //const auto csc = Utility::readFile("combined/test.comp.spv");
                //const auto csm = m_context.createShaderModule(csc);
                //vk::PipelineShaderStageCreateInfo pssci({}, vk::ShaderStageFlagBits::eCompute, csm, "main");
                //auto pl = m_context.getDevice().createPipelineLayoutUnique({});
                //vk::ComputePipelineCreateInfo cpci({}, pssci, pl.get());
                //auto cp = m_context.getDevice().createComputePipelineUnique(nullptr, cpci);

                if(m_useAsync)
                {
                    m_computeCommandBuffers.at(currentImage).reset({});

                    m_computeCommandBuffers.at(currentImage).begin(beginInfo);

                    //cmdBufForASUpdate.bindPipeline(vk::PipelineBindPoint::eCompute, cp.get());
                    //cmdBufForASUpdate.dispatch(1, 1, 1);
                }

                               
                const glm::mat4 oldModelMatrix = m_scene.getModelMatrices().at(m_animatedObjectID);
                glm::mat4 newModelMatrix4x4 = glm::translate(glm::rotate(glm::translate(oldModelMatrix, -glm::vec3(oldModelMatrix[3])), glm::radians(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(oldModelMatrix[3]));
                m_scene.setModelMatrix(m_animatedObjectID, newModelMatrix4x4);
                auto newModelMatrix = toRowMajor4x3(newModelMatrix4x4);
                cmdBufForASUpdate.updateBuffer(m_instanceBufferInfo.m_Buffer,
                    sizeof(GeometryInstance) * m_animatedObjectID + offsetof(GeometryInstance, transform),
                    sizeof(decltype(newModelMatrix)), glm::value_ptr(newModelMatrix));
                m_commandBuffers.at(currentImage).updateBuffer(m_modelMatrixBufferInfo.m_Buffer,
                    sizeof(decltype(newModelMatrix4x4)) * m_animatedObjectID,
                    sizeof(decltype(newModelMatrix4x4)), glm::value_ptr(newModelMatrix4x4));
                //BARRIER?
                vk::AccelerationStructureInfoNV asInfoTop(vk::AccelerationStructureTypeNV::eTopLevel, vk::BuildAccelerationStructureFlagBitsNV::eAllowUpdate, static_cast<uint32_t>(m_scene.getModelMatrices().size()), 0, nullptr);
                auto OwnCmdBuildAccelerationStructureNV = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(m_context.getDevice(), "vkCmdBuildAccelerationStructureNV"));
                OwnCmdBuildAccelerationStructureNV(cmdBufForASUpdate, reinterpret_cast<VkAccelerationStructureInfoNV*>(&asInfoTop), m_instanceBufferInfo.m_Buffer, 0, m_updateAS, m_topAS.m_AS, m_updateAS ? m_topAS.m_AS : nullptr, m_scratchBuffer.m_Buffer, 0);
                //m_commandBuffers.at(currentImage).buildAccelerationStructureNV(asInfoTop, m_instanceBufferInfo.m_Buffer, 0, m_updateAS, m_topAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
#undef MemoryBarrier
                vk::MemoryBarrier memoryBarrier(
                    vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV,
                    vk::AccessFlagBits::eAccelerationStructureReadNV
                );
#define MemoryBarrier __faststorefence
                cmdBufForASUpdate.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eRayTracingShaderNV, {}, memoryBarrier, nullptr, nullptr);

                m_timerManager.writeTimestampStop("0 AS Update", cmdBufForASUpdate, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, currentImage);

                if(m_useAsync)
                {
                    m_computeCommandBuffers.at(currentImage).end();

                    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_computeCommandBuffers.at(currentImage), 0, nullptr);

                    m_context.getComputeQueue().submit(submitInfo, m_computeFinishedFences.at(currentImage));
                    m_context.getComputeQueue().waitIdle();
                }

            }




            // 1st renderpass: render into g-buffer
            vk::ClearValue clearPosID(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, -1.0f });
            vk::ClearValue clearValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            std::array<vk::ClearValue, 5> clearColors = { clearPosID, clearValue, clearValue, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo(m_gbufferRenderpass, m_gbufferFramebuffers.at(currentImage), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());
            m_commandBuffers.at(currentImage).beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

            // execute command buffer which updates per-frame information
            m_commandBuffers.at(currentImage).executeCommands(m_perFrameSecondaryCommandBuffers.at(currentImage));

            // execute command buffers which contains rendering commands (for gbuffer)
            //TODO if this CB ever has to be recorded per-frame, it can be merged with perFrameSecondaryCommandBuffers
            m_commandBuffers.at(currentImage).executeCommands(m_gbufferSecondaryCommandBuffers.at(currentImage));

            m_commandBuffers.at(currentImage).endRenderPass();

            // upload per-frame information for RT
            vk::BufferMemoryBarrier rtPerFrameToTranser(
                vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, 0, VK_WHOLE_SIZE
            );

            m_commandBuffers.at(currentImage).pipelineBarrier(
                vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion, nullptr, rtPerFrameToTranser, nullptr
            );

            if (m_camera.hasChanged() || !m_accumulateRTSamples)
            {
                for (auto& n : m_sampleCounts)
                    n = 0;
                m_camera.resetChangeFlag();
            }

			glm::vec3 camPos = m_camera.getPosition();
			m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, cameraPosWorld), vk::ArrayProxy<const glm::vec3>{ camPos });
			m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, frameSampleCount), vk::ArrayProxy<const int32_t>{ m_sampleCounts.at(currentImage) });
			m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, RTAORadius), vk::ArrayProxy<const float>{ m_RTAORadius });
			m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, RTAOSampleCount), vk::ArrayProxy<const int32_t>{ m_numAOSamples });
			m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, RTReflectionSampleCount), vk::ArrayProxy<const int32_t>{ m_numRTReflectionSamples });
            m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, RTUseLowResReflections), vk::ArrayProxy<const int32_t>{ m_useLowResReflections });
            m_commandBuffers.at(currentImage).updateBuffer(m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, offsetof(RTperFrameInfoCombined, RTReflectionRoughnessThreshold), vk::ArrayProxy<const float>{ m_reflectionRoughnessThreshold });

            m_sampleCounts.at(currentImage)++;

            vk::BufferMemoryBarrier rtPerFrameToRead(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                m_rtPerFrameInfoBufferInfos.at(currentImage).m_Buffer, 0, VK_WHOLE_SIZE
            );

            m_commandBuffers.at(currentImage).pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eRayTracingShaderNV,
                vk::DependencyFlagBits::eByRegion, nullptr, rtPerFrameToRead, nullptr
            );

            // execute command buffers for RT shadows (has to come first because it transitions the gbuffer)
            m_commandBuffers.at(currentImage).executeCommands(m_rtSoftShadowsSecondaryCommandBuffers.at(currentImage));

            // execute command buffers for RTAO
            m_commandBuffers.at(currentImage).executeCommands(m_rtAOSecondaryCommandBuffers.at(currentImage));

			// execute command buffers for RT Reflections
            if(m_useLowResReflections == 0)
			    m_commandBuffers.at(currentImage).executeCommands(m_rtReflectionsSecondaryCommandBuffers.at(currentImage));
            else
                m_commandBuffers.at(currentImage).executeCommands(m_rtReflectionsLowResSecondaryCommandBuffers.at(currentImage));

            // 2nd renderpass: render into swapchain
            vk::ClearValue clearValue2(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            std::array<vk::ClearValue, 2> clearColors2 = { clearValue2, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo2(m_fullscreenLightingRenderpass, m_swapChainFramebuffers.at(currentImage), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors2.size()), clearColors2.data());
            m_commandBuffers.at(currentImage).beginRenderPass(renderpassInfo2, vk::SubpassContents::eSecondaryCommandBuffers);

            m_commandBuffers.at(currentImage).executeCommands(m_fullscreenLightingSecondaryCommandBuffers.at(currentImage));

            m_commandBuffers.at(currentImage).endRenderPass();

            m_commandBuffers.at(currentImage).end();

            if (m_animate && m_useAsync)
            {
                m_context.getDevice().waitForFences(m_computeFinishedFences.at(currentImage), VK_TRUE, std::numeric_limits<uint64_t>::max());
                m_context.getDevice().resetFences(m_computeFinishedFences.at(currentImage));
            }
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
                if (ImGui::BeginMenu("Shaders"))
                {
                    if (ImGui::Button("Reload: g-buffer"))
                    {
                        m_context.getGraphicsQueue().waitIdle();
                        m_context.getDevice().destroyPipeline(m_gbufferGraphicsPipeline);
                        m_context.getDevice().destroyPipelineLayout(m_gbufferPipelineLayout);
                        createGBufferPipeline();
                        createAllCommandBuffers();
                    }
                    if (ImGui::Button("Reload: fullscreen lighting"))
                    {
                        m_context.getGraphicsQueue().waitIdle();
                        m_context.getDevice().destroyPipeline(m_fullscreenLightingPipeline);
                        m_context.getDevice().destroyPipelineLayout(m_fullscreenLightingPipelineLayout);
                        createFullscreenLightingPipeline();
                        createAllCommandBuffers();
                    }
                    if (ImGui::Button("Reload: soft shadows (rt)"))
                    {
                        m_context.getGraphicsQueue().waitIdle();
                        m_context.getDevice().destroyPipeline(m_rtSoftShadowsPipeline);
                        m_context.getDevice().destroyPipelineLayout(m_rtSoftShadowsPipelineLayout);
                        createRTSoftShadowsPipeline();
                        createAllCommandBuffers();
                    }
                    if (ImGui::Button("Reload: ambient occlusion (rt)"))
                    {
                        m_context.getGraphicsQueue().waitIdle();
                        m_context.getDevice().destroyPipeline(m_rtAOPipeline);
                        m_context.getDevice().destroyPipelineLayout(m_rtAOPipelineLayout);
                        createRTAOPipeline();
                        createAllCommandBuffers();
                    }
					if (ImGui::Button("Reload: reflections (rt)"))
					{
                        m_context.getGraphicsQueue().waitIdle();
                        m_context.getDevice().destroyPipeline(m_rtReflectionsPipeline);
                        m_context.getDevice().destroyPipelineLayout(m_rtReflectionsPipelineLayout);
						createRTReflectionPipeline();
						createAllCommandBuffers();
					}
                    ImGui::EndMenu();
                }
                m_lightManager.lightGUI(m_lightBufferInfos.at(0), m_lightBufferInfos.at(1), m_lightBufferInfos.at(2), true);
                if (ImGui::BeginMenu("Ray Tracing"))
                {
                    if(ImGui::Checkbox("Accumulate Samples", &m_accumulateRTSamples))
                    {
                        m_animate = false;
                        m_timerManager.setGuiActiveStatusForTimer("0 AS Update", m_animate);
                    }
                    ImGui::SliderFloat("RTAO Radius", &m_RTAORadius, 0.1f, 100.0f);
                    ImGui::SliderInt("RTAO Samples", &m_numAOSamples, 1, 64);
					ImGui::SliderInt("RT Reflection Samples", &m_numRTReflectionSamples, 1, 64);
                    ImGui::RadioButton("Full Resolution Reflections", &m_useLowResReflections, 0); ImGui::SameLine();
                    ImGui::RadioButton("Low Resolution Reflections", &m_useLowResReflections, 1);
                    ImGui::SliderFloat("Roughness Threshold for Reflections", &m_reflectionRoughnessThreshold, 0.0f, 1.0f);
                    if (m_reflectionRoughnessThreshold > 0.0f) m_accumulateRTSamples = false;
                    ImGui::EndMenu();
                }
				if (ImGui::BeginMenu("Lighting"))
				{
					ImGui::SliderFloat("Exposure", &m_exposure, 0.1f, 100.0f);
					ImGui::EndMenu();
				}
                if (ImGui::BeginMenu("Animate"))
                {
                    if(ImGui::Checkbox("Animate?", &m_animate))
                    {
                        m_timerManager.setGuiActiveStatusForTimer("0 AS Update", m_animate);
                    }
                    if(m_animate)
                    {
                        ImGui::InputInt("Object ID", &m_animatedObjectID);

                        if (m_animatedObjectID > m_scene.getModelMatrices().size()-1)
                            m_animatedObjectID = static_cast<int>(m_scene.getModelMatrices().size()-1);
                        if (m_animatedObjectID < 0)
                            m_animatedObjectID = 0;

                        ImGui::RadioButton("Rebuild BVH", &m_updateAS, 0); ImGui::SameLine();
                        ImGui::RadioButton("Update BVH", &m_updateAS, 1);
                        ImGui::Checkbox("Use Async Compute", &m_useAsync);
                        m_accumulateRTSamples = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Performance"))
                {
                    ImGui::Dummy({ 600.0f, 0 });
                    m_timerManager.drawTimerGUIs();

                    ImGui::Checkbox("Wait for device idle after every frame", &m_waitIdleAfterFrame);
                    ImGui::SameLine();
                    if (ImGui::Button("Write Timediffs to file"))
                        m_timerManager.dumpActiveTimerDiffsToFile();

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
            m_timerManager.writeTimestampStart("6 ImGui", m_imguiCommandBuffers.at(imageIndex), vk::PipelineStageFlagBits::eAllGraphics, imageIndex);

            m_imguiCommandBuffers.at(imageIndex).beginRenderPass(imguiRenderpassInfo, vk::SubpassContents::eInline);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_imguiCommandBuffers.at(imageIndex));
            m_imguiCommandBuffers.at(imageIndex).endRenderPass();
            m_timer.cmdWriteTimestampStart(m_imguiCommandBuffers.at(imageIndex), vk::PipelineStageFlagBits::eAllGraphics, m_queryPool, 0);
            m_timerManager.writeTimestampStop("6 ImGui", m_imguiCommandBuffers.at(imageIndex), vk::PipelineStageFlagBits::eAllGraphics, imageIndex);

            m_imguiCommandBuffers.at(imageIndex).end();

            // wait rest of the rendering, submit
            const std::array waitSemaphores = { m_graphicsRenderFinishedSemaphores.at(m_currentFrame) };
            const std::array waitStages = { static_cast<vk::PipelineStageFlags>(vk::PipelineStageFlagBits::eColorAttachmentOutput) };
            const std::array signalSemaphores = { m_guiFinishedSemaphores.at(m_currentFrame) };

            const vk::SubmitInfo submitInfo(1, waitSemaphores.data(), waitStages.data(), 1, &m_imguiCommandBuffers.at(imageIndex), 1, signalSemaphores.data());

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
                if(m_waitIdleAfterFrame)
                {
                    m_context.getDevice().waitIdle();
                }
                m_timer.acquireCurrentTimestamp(m_context.getDevice(), m_queryPool);
                m_timerManager.queryAllTimerResults(m_currentFrame);
            }

            m_context.getDevice().waitIdle();
        }

    private:

        BufferInfo m_vertexBufferInfo;
        BufferInfo m_indexBufferInfo;
        BufferInfo m_indirectDrawBufferInfo;
        BufferInfo m_modelMatrixBufferInfo;
        BufferInfo m_materialBufferInfo;


        std::vector<ImageInfo> m_allImages;
        std::vector<vk::ImageView> m_allImageViews;
        std::vector<vk::Sampler> m_allImageSamplers;

        Pilotview m_camera;
        glm::mat4 m_projection;
        bool m_projectionChanged;

        TimerManager m_timerManager;

        PBRScene m_scene;

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
        std::vector<vk::DescriptorSet> m_fullScreenLightingDescriptorSets;

        vk::DescriptorPool m_combinedDescriptorPool;

        // Light Stuff
        std::vector<BufferInfo> m_lightBufferInfos;
        vk::DescriptorSetLayout m_lightDescriptorSetLayout;
        vk::DescriptorSet m_lightDescriptorSet;
        PBRLightManager m_lightManager;

		float m_exposure = 8.0f;

        // Sync Objects

        // RT Stuff
        ASInfo m_topAS;
        std::vector<ASInfo> m_bottomASs;
        BufferInfo m_instanceBufferInfo;

        BufferInfo m_scratchBuffer;

        BufferInfo m_offsetBufferInfo;
        //BufferInfo m_transformBufferInfo;

        std::vector<ImageInfo> m_randomImageInfos;
        std::vector<vk::ImageView> m_randomImageViews;

        std::vector<int32_t> m_sampleCounts;
        std::vector<BufferInfo> m_rtPerFrameInfoBufferInfos;
        int32_t m_numAOSamples = 1;
        float m_RTAORadius = 100.0f;
		int32_t m_numRTReflectionSamples = 1;

        // soft shadow stuff
        vk::DescriptorSetLayout m_rtSoftShadowsDescriptorSetLayout;
        vk::PipelineLayout m_rtSoftShadowsPipelineLayout;
        vk::Pipeline m_rtSoftShadowsPipeline;
        std::vector<vk::DescriptorSet> m_rtSoftShadowsDescriptorSets;
        BufferInfo m_rtSoftShadowSBTInfo;
        std::vector<vk::CommandBuffer> m_rtSoftShadowsSecondaryCommandBuffers;
        
        bool m_accumulateRTSamples = true;

        std::vector<ImageInfo> m_rtSoftShadowPointImageInfos;
        std::vector<vk::ImageView> m_rtSoftShadowPointImageViews;
        std::vector<vk::Sampler> m_rtSoftShadowPointImageSamplers;

        std::vector<ImageInfo> m_rtSoftShadowSpotImageInfos;
        std::vector<vk::ImageView> m_rtSoftShadowSpotImageViews;
        std::vector<vk::Sampler> m_rtSoftShadowSpotImageSamplers;

        std::vector<ImageInfo> m_rtSoftShadowDirectionalImageInfos;
        std::vector<vk::ImageView> m_rtSoftShadowDirectionalImageViews;
        std::vector<vk::Sampler> m_rtSoftShadowDirectionalImageSamplers;

        vk::DescriptorSetLayout m_shadowImageStoreDescriptorSetLayout;
        vk::DescriptorSetLayout m_allRTImageSampleDescriptorSetLayout;
        std::vector<vk::DescriptorSet> m_allRTImageSampleDescriptorSets;
        std::vector<vk::DescriptorSet> m_shadowImageStoreDescriptorSets;


        // rtao stuff
        vk::DescriptorSetLayout m_rtAODescriptorSetLayout;
        vk::DescriptorSetLayout m_rtAOImageStoreDescriptorSetLayout;
        std::vector<vk::DescriptorSet> m_rtAOImageStoreDescriptorSets;

        vk::PipelineLayout m_rtAOPipelineLayout;
        vk::Pipeline m_rtAOPipeline;
        std::vector<vk::DescriptorSet> m_rtAODescriptorSets;
        BufferInfo m_rtAOSBTInfo;
        std::vector<vk::CommandBuffer> m_rtAOSecondaryCommandBuffers;

        std::vector<ImageInfo> m_rtAOImageInfos;
        std::vector<vk::ImageView> m_rtAOImageViews;
        std::vector<vk::Sampler> m_rtAOImageSamplers;

		// reflection stuff
		std::vector<ImageInfo> m_rtReflectionImageInfos;
		std::vector<vk::ImageView> m_rtReflectionImageViews;
		std::vector<vk::Sampler> m_rtReflectionImageSamplers;

        std::vector<ImageInfo> m_rtReflectionLowResImageInfos;
        std::vector<vk::ImageView> m_rtReflectionLowResImageViews;
        std::vector<vk::Sampler> m_rtReflectionLowResImageSamplers;

		vk::DescriptorSetLayout m_rtReflectionsDescriptorSetLayout;
		vk::PipelineLayout m_rtReflectionsPipelineLayout;
		vk::Pipeline m_rtReflectionsPipeline;
		std::vector<vk::DescriptorSet> m_rtReflectionsDescriptorSets;
		BufferInfo m_rtReflectionsSBTInfo;
		std::vector<vk::CommandBuffer> m_rtReflectionsSecondaryCommandBuffers;
        std::vector<vk::CommandBuffer> m_rtReflectionsLowResSecondaryCommandBuffers;

        [[nodiscard]] glm::mat3x4 toRowMajor4x3(const glm::mat4 & in) const
        {
            return glm::mat3x4(glm::rowMajor4(in));
        }

        bool m_animate = false;
        int m_animatedObjectID = 154;
        int m_updateAS = 0;
        bool m_useAsync = false;
        bool m_waitIdleAfterFrame = false;

        int32_t m_useLowResReflections = 0;
        float m_reflectionRoughnessThreshold = 0.0f;

        std::vector<vk::Fence> m_computeFinishedFences;
        std::vector<vk::CommandBuffer> m_computeCommandBuffers;
    };
}

int main()
{
    vg::RTCombinedApp app;

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