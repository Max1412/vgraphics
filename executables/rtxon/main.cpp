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
        MultiApp() : BaseApp({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters", "VK_NV_ray_tracing" }),
			m_camera(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height),
			m_scene("Sponza/sponza.obj")
        {
            createRenderPass();
            //createDescriptorSetLayout();

            createGraphicsPipeline();
            createCommandPools();

            createDepthResources();
            createSwapchainFramebuffers(m_renderpass);

            createSceneInformation("sponza/");

            createVertexBuffer();
            createIndexBuffer();
            //createIndirectDrawBuffer();
            //createPerGeometryBuffers();

            createAccelerationStructure();
            createRTPipeline();

            createPerFrameInformation();
            createDescriptorPool();
            //createDescriptorSets();

            createCommandBuffers();
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
            //vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indirectDrawBufferInfo.m_Buffer), m_indirectDrawBufferInfo.m_BufferAllocation);

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

            m_context.getDevice().destroyDescriptorPool(m_rayTracingDescriptorPool);
            m_context.getDevice().destroyDescriptorSetLayout(m_rayTracingDescriptorSetLayout);

			vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_offsetBufferInfo.m_Buffer), m_offsetBufferInfo.m_BufferAllocation);
			vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_instanceBufferInfo.m_Buffer), m_instanceBufferInfo.m_BufferAllocation);
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_scratchBuffer.m_Buffer), m_scratchBuffer.m_BufferAllocation);
			vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_sbtInfo.m_Buffer), m_sbtInfo.m_BufferAllocation);

			
			m_context.getDevice().destroyAccelerationStructureNV(m_topAS.m_AS);
			vmaFreeMemory(m_context.getAllocator(), m_topAS.m_BufferAllocation);

			for(const auto& as : m_bottomASs)
			{
				m_context.getDevice().destroyAccelerationStructureNV(as.m_AS);
				vmaFreeMemory(m_context.getAllocator(), as.m_BufferAllocation);
			}

			m_context.getDevice().destroyPipeline(m_rayTracingPipeline);
			m_context.getDevice().destroyPipelineLayout(m_rayTracingPipelineLayout);

            m_context.getDevice().destroyRenderPass(m_renderpass);
            // cleanup here
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
                    0.0f, VK_TRUE, 16.0f, VK_FALSE, vk::CompareOp::eAlways, 0.0f, static_cast<float>(imageInfo.mipLevels), vk::BorderColor::eIntOpaqueBlack, VK_FALSE
                );
                m_allImageSamplers.push_back(m_context.getDevice().createSampler(samplerInfo));

            }

			m_context.getLogger()->info("Texture loading complete.");

        }

        void createVertexBuffer()
        {
            m_vertexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getVertices(), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);

        }

        void createIndexBuffer()
        {
            m_indexBufferInfo = fillBufferTroughStagedTransfer(m_scene.getIndices(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        }

        //void createIndirectDrawBuffer()
        //{
        //    m_indirectDrawBufferInfo = fillBufferTroughStagedTransfer(m_scene.getDrawCommandData(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        //}

        //void createPerGeometryBuffers()
        //{
        //    m_modelMatrixBufferInfo = fillBufferTroughStagedTransfer(m_scene.getModelMatrices(), vk::BufferUsageFlagBits::eStorageBuffer);
        //}

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
                int m_diffTextureID = -1;
                int m_specTextureID = -1;
            };

            std::vector<OffsetInfo> offsetInfos;
            int32_t indexOffset0 = 0;
            int j = 0;
            for(const PerMeshInfo& meshInfo : m_scene.getDrawCommandData())
            {
                offsetInfos.push_back(OffsetInfo{ meshInfo.vertexOffset, indexOffset0, meshInfo.texIndex, meshInfo.texSpecIndex });
                indexOffset0 += meshInfo.indexCount;

                j++;
            }
            m_offsetBufferInfo = fillBufferTroughStagedTransfer(offsetInfos, vk::BufferUsageFlagBits::eStorageBuffer);

            std::vector<vk::GeometryNV> geometryVec;

            // TODO 1 Mesh = 1 BLAS + GeometryInstance w/ ModelMatrix as Transform

            const auto toRowMajor4x3 = [](const glm::mat4& in) { return glm::mat3x4(glm::rowMajor4(in)); };

            //std::vector<glm::mat4x3> transforms;
            //for (const auto& modelMatrix : m_scene.getModelMatrices())
            //    transforms.emplace_back(toRowMajor4x3(modelMatrix));

            //m_transformBufferInfo = fillBufferTroughStagedTransfer(transforms, vk::BufferUsageFlagBits::eRayTracingNV);

            // currently: 1 geometry = 1 mesh
            size_t c = 0;
            uint64_t indexOffset = 0;
            for(const PerMeshInfo& meshInfo : m_scene.getDrawCommandData())
            {

                uint32_t vertexCount = 0;

                //todo check this calculation
                if (c < m_scene.getDrawCommandData().size()-1)
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
                triangles.transformOffset = 0;// i * sizeof(glm::mat4x3);
				
                indexOffset += meshInfo.indexCount;

                vk::GeometryDataNV geoData(triangles, {});
                vk::GeometryNV geom(vk::GeometryTypeNV::eTriangles, geoData, vk::GeometryFlagBitsNV::eOpaque);

                geometryVec.push_back(geom);
                c++;
            }

            auto createActualAcc = [&]
            (vk::AccelerationStructureTypeNV type, uint32_t geometryCount, vk::GeometryNV* geometries, uint32_t instanceCount) -> ASInfo
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
                vk::AccelerationStructureInfoNV asInfo(type, {}, instanceCount, geometryCount, geometries);
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

            for(auto& geometry : geometryVec)
                m_bottomASs.push_back(createActualAcc(vk::AccelerationStructureTypeNV::eBottomLevel, 1, &geometry, 0));


            struct GeometryInstance
            {
				// row major 4x3 model matrix
                float transform[12];

				// instanceId is exposed as gl_InstanceCustomIndexNV
                uint32_t instanceId : 24;

				// mask to exclude hitting this geometry. if rayMask & instance.mask == 0, the geometry will NOT be hit
                uint32_t mask : 8;

				// instance offset is basically the hit shader index. 0 if only one hit shader is present
                uint32_t instanceOffset : 24;

				// any of VkGeometryInstanceFlagBitsNV 
                uint32_t flags : 8;

				// bottom level AS handle this instance corresponds to
                uint64_t accelerationStructureHandle;
            };

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
            m_instanceBufferInfo = fillBufferTroughStagedTransfer(instances, vk::BufferUsageFlagBits::eRayTracingNV);


            m_topAS = createActualAcc(vk::AccelerationStructureTypeNV::eTopLevel, 0, nullptr, 1);

            auto GetScratchBufferSize = [&](vk::AccelerationStructureNV handle)
            {
                vk::AccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo(vk::AccelerationStructureMemoryRequirementsTypeNV::eBuildScratch, handle);

                vk::MemoryRequirements2 memoryRequirements = m_context.getDevice().getAccelerationStructureMemoryRequirementsNV(memoryRequirementsInfo);

                VkDeviceSize result = memoryRequirements.memoryRequirements.size;
                return result;
            };

            vk::DeviceSize maxBLASSize = 0;
            for(const auto& blas : m_bottomASs)
            {
                maxBLASSize = std::max(GetScratchBufferSize(blas.m_AS), maxBLASSize);
            }

            //VkDeviceSize bottomAccelerationStructureBufferSize = GetScratchBufferSize(m_bottomAS.m_AS);
            VkDeviceSize topAccelerationStructureBufferSize = GetScratchBufferSize(m_topAS.m_AS);
            VkDeviceSize scratchBufferSize = std::max(maxBLASSize, topAccelerationStructureBufferSize);

            m_scratchBuffer = createBuffer(scratchBufferSize, vk::BufferUsageFlagBits::eRayTracingNV, VMA_MEMORY_USAGE_GPU_ONLY);

            auto cmdBuf = beginSingleTimeCommands(m_commandPool);
#undef MemoryBarrier
            vk::MemoryBarrier memoryBarrier(
                vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV,
                vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV
            );
#define MemoryBarrier __faststorefence

            //todo remove this when the SDK update happened
            auto OwnCmdBuildAccelerationStructureNV = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(m_context.getDevice(), "vkCmdBuildAccelerationStructureNV"));

            for(int i = 0; i < geometryVec.size(); i++)
            {
                vk::AccelerationStructureInfoNV asInfoBot(vk::AccelerationStructureTypeNV::eBottomLevel, {}, 0, 1, &geometryVec.at(i));
                OwnCmdBuildAccelerationStructureNV(cmdBuf, reinterpret_cast<VkAccelerationStructureInfoNV*>(&asInfoBot), nullptr, 0, VK_FALSE, m_bottomASs.at(i).m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
                //cmdBuf.buildAccelerationStructureNV(asInfoBot, nullptr, 0, VK_FALSE, m_bottomAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
                cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eRayTracingShaderNV, {}, memoryBarrier, nullptr, nullptr);
            
            }

            vk::AccelerationStructureInfoNV asInfoTop(vk::AccelerationStructureTypeNV::eTopLevel, {}, static_cast<uint32_t>(instances.size()), 0, nullptr);
            OwnCmdBuildAccelerationStructureNV(cmdBuf, reinterpret_cast<VkAccelerationStructureInfoNV*>(&asInfoTop), m_instanceBufferInfo.m_Buffer, 0, VK_FALSE, m_topAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
            //cmdBuf.buildAccelerationStructureNV(asInfoTop, m_instanceBufferInfo.m_Buffer, 0, VK_FALSE, m_topAS.m_AS, nullptr, m_scratchBuffer.m_Buffer, 0);
            cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eRayTracingShaderNV, {}, memoryBarrier, nullptr, nullptr);

            endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);
        }

        void createRTPipeline()
        {
            //// 1. DSL

            // AS
            vk::DescriptorSetLayoutBinding asLB(0, vk::DescriptorType::eAccelerationStructureNV, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            // Image Load/Store for output
            vk::DescriptorSetLayoutBinding oiLB(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenNV, nullptr);
            // texture array
            vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_allImages.size()), vk::ShaderStageFlagBits::eClosestHitNV, nullptr);

            // buffers
            vk::DescriptorSetLayoutBinding vertexBufferLB(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
            vk::DescriptorSetLayoutBinding indexBufferLB(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitNV, nullptr);
            vk::DescriptorSetLayoutBinding offsetBufferLB(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitNV, nullptr);


            std::array<vk::DescriptorSetLayoutBinding, 6> bindings = { asLB, oiLB, allTexturesLayoutBinding, vertexBufferLB, indexBufferLB, offsetBufferLB };

            vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

            m_rayTracingDescriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

            //// 2. Create Pipeline

            const auto rgenShaderCode = Utility::readFile("rtxon/rt_06_shaders.rgen.spv");
            const auto rgenShaderModule = m_context.createShaderModule(rgenShaderCode);

            const auto chitShaderCode = Utility::readFile("rtxon/rt_06_shaders.rchit.spv");
            const auto chitShaderModule = m_context.createShaderModule(chitShaderCode);

            const auto missShaderCode = Utility::readFile("rtxon/rt_06_shaders.rmiss.spv");
            const auto missShaderModule = m_context.createShaderModule(missShaderCode);

            // specialization constant for the number of textures
            vk::SpecializationMapEntry mapEntry(0, 0, sizeof(int32_t));
            int32_t numTextures = static_cast<int32_t>(m_scene.getIndexedDiffuseTexturePaths().size());
            vk::SpecializationInfo numTexturesSpecInfo(1, &mapEntry, sizeof(int32_t), &numTextures);

            const std::array<vk::PipelineShaderStageCreateInfo, 3> rtShaderStageInfos = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eRaygenNV, rgenShaderModule, "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitNV, chitShaderModule, "main", &numTexturesSpecInfo),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissNV, missShaderModule, "main"),
            };

            std::array<vk::PushConstantRange, 1> vpcr = {
                vk::PushConstantRange{vk::ShaderStageFlagBits::eRaygenNV, 0, 2 * sizeof(glm::mat4)},
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo({}, 1, &m_rayTracingDescriptorSetLayout, static_cast<uint32_t>(vpcr.size()), vpcr.data());

            m_rayTracingPipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutCreateInfo);

            std::array<vk::RayTracingShaderGroupCreateInfoNV, 3> shaderGroups = {
                // group 0: raygen
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 0, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                // group 1: closest hit
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup, VK_SHADER_UNUSED_NV, 1, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV},
                // group 2: miss
                vk::RayTracingShaderGroupCreateInfoNV{vk::RayTracingShaderGroupTypeNV::eGeneral, 2, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV}
            };

            vk::RayTracingPipelineCreateInfoNV rayPipelineInfo({},
				static_cast<uint32_t>(rtShaderStageInfos.size()), rtShaderStageInfos.data(),
				static_cast<uint32_t>(shaderGroups.size()), shaderGroups.data(),
                m_context.getRaytracingProperties().maxRecursionDepth,
                m_rayTracingPipelineLayout,
                nullptr, 0
            );

            m_rayTracingPipeline = m_context.getDevice().createRayTracingPipelinesNV(nullptr, rayPipelineInfo).at(0);

            //// 3. Create Shader Binding Table

            const uint32_t shaderBindingTableSize = m_context.getRaytracingProperties().shaderGroupHandleSize * rayPipelineInfo.groupCount;

            m_sbtInfo = createBuffer(shaderBindingTableSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
            void* mappedData;
            vmaMapMemory(m_context.getAllocator(), m_sbtInfo.m_BufferAllocation, &mappedData);
            const auto res = m_context.getDevice().getRayTracingShaderGroupHandlesNV(m_rayTracingPipeline, 0, rayPipelineInfo.groupCount, shaderBindingTableSize, mappedData);
            if (res != vk::Result::eSuccess) throw std::runtime_error("Failed to retrieve Shader Group Handles");
            vmaUnmapMemory(m_context.getAllocator(), m_sbtInfo.m_BufferAllocation);

            //// 4. Create Descriptor Set

            // deviation from tutorial: I'm creating multiple descriptor sets, and binding the one with the current swap chain image

            //todo delete this pool, use the other existing one instead (?)
            std::array<vk::DescriptorPoolSize, 4> poolSizes = { 
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
                vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureNV, 1},
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4096),
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 3}

            };

            vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo({}, static_cast<uint32_t>(m_swapChainFramebuffers.size()), static_cast<uint32_t>(poolSizes.size()), poolSizes.data());
            m_rayTracingDescriptorPool = m_context.getDevice().createDescriptorPool(descriptorPoolCreateInfo);

            // create n descriptor sets
            std::vector<vk::DescriptorSetLayout> dsls(m_swapChainFramebuffers.size(), m_rayTracingDescriptorSetLayout);
            vk::DescriptorSetAllocateInfo desSetAllocInfo(m_rayTracingDescriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), dsls.data());
            m_rayTracingDescriptorSets = m_context.getDevice().allocateDescriptorSets(desSetAllocInfo);


            std::vector<vk::DescriptorImageInfo> allImageInfos;
            for (int i = 0; i < m_allImages.size(); i++)
            {
                allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
            }

            for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
            {
                vk::WriteDescriptorSetAccelerationStructureNV descriptorSetAccelerationStructureInfo(1, &m_topAS.m_AS);
                vk::WriteDescriptorSet accelerationStructureWrite(m_rayTracingDescriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eAccelerationStructureNV, nullptr, nullptr, nullptr);
                accelerationStructureWrite.setPNext(&descriptorSetAccelerationStructureInfo); // pNext is assigned here!!!
                
                // the tutorial always writes to the same image. Here, multiple descriptor sets each corresponding to a swapchain image are created
                vk::DescriptorImageInfo descriptorOutputImageInfo(nullptr, m_context.getSwapChainImageViews().at(i), vk::ImageLayout::eGeneral); // maybe layout colorAttachment?
                vk::WriteDescriptorSet outputImageWrite(m_rayTracingDescriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eStorageImage, &descriptorOutputImageInfo, nullptr, nullptr);

                vk::WriteDescriptorSet descWriteAllImages(m_rayTracingDescriptorSets.at(i), 2, 0, static_cast<uint32_t>(m_allImages.size()), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

                vk::DescriptorBufferInfo vbInfo(m_vertexBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet descWriteVertexBuffer(m_rayTracingDescriptorSets.at(i), 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &vbInfo, nullptr);
                vk::DescriptorBufferInfo ibInfo(m_indexBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet descWriteIndexBuffer(m_rayTracingDescriptorSets.at(i), 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &ibInfo, nullptr);
                vk::DescriptorBufferInfo obInfo(m_offsetBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
                vk::WriteDescriptorSet descWriteOffsetBuffer(m_rayTracingDescriptorSets.at(i), 5, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &obInfo, nullptr);

                std::array<vk::WriteDescriptorSet, 6> descriptorWrites = { accelerationStructureWrite, outputImageWrite, descWriteAllImages, descWriteVertexBuffer, descWriteIndexBuffer, descWriteOffsetBuffer };
                m_context.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
            }
        }

        void createCommandBuffers()
        {
            vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

            //// static secondary buffers (containing draw calls), never change
            //vk::CommandBufferAllocateInfo secondaryCmdAllocInfo(m_commandPool, vk::CommandBufferLevel::eSecondary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));
            //m_staticSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);

            //// dynamic secondary buffers (containing per-frame information), getting re-recorded if necessary
            //m_perFrameSecondaryCommandBuffers = m_context.getDevice().allocateCommandBuffers(secondaryCmdAllocInfo);

        }

        void createPerFrameInformation()
        {
            m_camera = Pilotview(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height);
            m_camera.setSensitivity(0.01f);
            m_projection = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10000.0f);
            m_projection[1][1] *= -1;

            m_camera.update(m_context.getWindow());

        }

        void recordPerFrameCommandBuffers(uint32_t currentImage) override
        {
            ////// Secondary Command Buffer with per-frame information (TODO: this can be done in a seperate thread)
            m_commandBuffers.at(currentImage).reset({});

            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);
            m_commandBuffers.at(currentImage).begin(beginInfo);


            // transition image to make it accessible by imageStore
            transitionInCmdBuf(m_context.getSwapChainImages().at(currentImage), m_context.getSwapChainImageFormat(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, m_commandBuffers.at(currentImage));

            m_commandBuffers.at(currentImage).bindPipeline(vk::PipelineBindPoint::eRayTracingNV, m_rayTracingPipeline);
            m_commandBuffers.at(currentImage).bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, m_rayTracingPipelineLayout, 0, 1, &m_rayTracingDescriptorSets.at(currentImage), 0, nullptr);

            m_camera.update(m_context.getWindow());
            m_commandBuffers.at(currentImage).pushConstants(m_rayTracingPipelineLayout, vk::ShaderStageFlagBits::eRaygenNV, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
            m_commandBuffers.at(currentImage).pushConstants(m_rayTracingPipelineLayout, vk::ShaderStageFlagBits::eRaygenNV, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));

            //m_commandBuffers.at(i).traceRaysNV(
            auto OwnCmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(m_context.getDevice(), "vkCmdTraceRaysNV"));
            OwnCmdTraceRays(m_commandBuffers.at(currentImage),
                m_sbtInfo.m_Buffer, 0, // raygen
                m_sbtInfo.m_Buffer, 2 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // miss
                m_sbtInfo.m_Buffer, 1 * m_context.getRaytracingProperties().shaderGroupHandleSize, m_context.getRaytracingProperties().shaderGroupHandleSize, // hit
                nullptr, 0, 0, // callable
                m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height, 1
            );


            // transition image for to use it for imgui
            transitionInCmdBuf(m_context.getSwapChainImages().at(currentImage), m_context.getSwapChainImageFormat(), vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1, m_commandBuffers.at(currentImage));


            m_commandBuffers.at(currentImage).end();

        }

        // todo change this when uniform buffer changes
        void createDescriptorPool()
        {
            //// todo change descriptor count here to have as many of the type specified here as I want
            //vk::DescriptorPoolSize perMeshInformationIndirectDrawSSBO(vk::DescriptorType::eStorageBuffer, 1);
            //vk::DescriptorPoolSize poolSizemodelMatrixSSBO(vk::DescriptorType::eStorageBuffer, 1);
            //vk::DescriptorPoolSize poolSizeCombinedImageSampler(vk::DescriptorType::eCombinedImageSampler, 1);
            //vk::DescriptorPoolSize poolSizeAllImages(vk::DescriptorType::eCombinedImageSampler, 4096);

            //std::array<vk::DescriptorPoolSize, 4> poolSizes = { poolSizemodelMatrixSSBO, poolSizeCombinedImageSampler, perMeshInformationIndirectDrawSSBO, poolSizeAllImages };

            //vk::DescriptorPoolCreateInfo poolInfo({}, 1, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

            //m_descriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);
        }

        //void createDescriptorSets()
        //{
        //    // only 1 set needed
        //    vk::DescriptorSetAllocateInfo allocInfo(m_descriptorPool, 1, &m_descriptorSetLayout);
        //    m_descriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

        //    // model matrix buffer
        //    vk::DescriptorBufferInfo bufferInfo(m_modelMatrixBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
        //    vk::WriteDescriptorSet descWrite(m_descriptorSets.at(0), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo, nullptr);

        //    // indirect draw buffer + add. info as ssbo
        //    vk::DescriptorBufferInfo perMeshInformationIndirectDrawSSBOInfo(m_indirectDrawBufferInfo.m_Buffer, 0, VK_WHOLE_SIZE);
        //    vk::WriteDescriptorSet descWrite2(m_descriptorSets.at(0), 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &perMeshInformationIndirectDrawSSBOInfo, nullptr);

        //    std::vector<vk::DescriptorImageInfo> allImageInfos;
        //    for(int i = 0; i < m_allImages.size(); i++)
        //    {
        //        allImageInfos.emplace_back(m_allImageSamplers.at(i), m_allImageViews.at(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        //    }
        //    vk::WriteDescriptorSet descWriteAllImages(m_descriptorSets.at(0), 3, 0, m_allImages.size(), vk::DescriptorType::eCombinedImageSampler, allImageInfos.data(), nullptr, nullptr);

        //    std::array<vk::WriteDescriptorSet, 3> descriptorWrites = { descWrite, descWrite2, descWriteAllImages };
        //    m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        //}

        //void createDescriptorSetLayout()
        //{
        //    vk::DescriptorSetLayoutBinding modelMatrixSSBOLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr);
        //    vk::DescriptorSetLayoutBinding perMeshInformationIndirectDrawSSBOLB(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

        //    vk::DescriptorSetLayoutBinding samplerLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

        //    vk::DescriptorSetLayoutBinding allTexturesLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, m_scene.getIndexedDiffuseTexturePaths().size(), vk::ShaderStageFlagBits::eFragment, nullptr);


        //    std::array<vk::DescriptorSetLayoutBinding, 4> bindings = { modelMatrixSSBOLayoutBinding, samplerLayoutBinding, perMeshInformationIndirectDrawSSBOLB, allTexturesLayoutBinding };

        //    vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

        //    m_descriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);
        //}


        // base
        void recreateSwapChain() override
        {
            int width = 0, height = 0;
            while (width == 0 || height == 0)
            {
                glfwGetFramebufferSize(m_context.getWindow(), &width, &height);
                glfwWaitEvents();
            }

            //m_projection = glm::perspective(glm::radians(45.0f), width / static_cast<float>(height), 0.1f, 10000.0f);
            //m_projection[1][1] *= -1;
            //m_projectionChanged = true;
            // todo set camera width, height ?

            m_context.getDevice().waitIdle();

            cleanUpSwapchain();

            m_context.createSwapChain();
            m_context.createImageViews();
            createRenderPass();
            createGraphicsPipeline();
            createDepthResources();
            createSwapchainFramebuffers(m_renderpass);
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

            //m_context.getDevice().destroyPipeline(m_graphicsPipeline);
            //m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
            m_context.getDevice().destroyRenderPass(m_renderpass);

            for (auto& sciv : m_context.getSwapChainImageViews())
                m_context.getDevice().destroyImageView(sciv);

            m_context.getDevice().destroySwapchainKHR(m_context.getSwapChain());
        }

        void createGraphicsPipeline()
        {
            //const auto vertShaderCode = Utility::readFile("multi/shader.vert.spv");
            //const auto fragShaderCode = Utility::readFile("multi/shader.frag.spv");

            //const auto vertShaderModule = m_context.createShaderModule(vertShaderCode);
            //const auto fragShaderModule = m_context.createShaderModule(fragShaderCode);

            //// specialization constant for the number of textures
            //vk::SpecializationMapEntry mapEntry(0, 0, sizeof(int32_t));
            //int32_t numTextures = static_cast<int32_t>(m_scene.getIndexedDiffuseTexturePaths().size());
            //vk::SpecializationInfo numTexturesSpecInfo(1, &mapEntry, sizeof(int32_t), &numTextures);

            //const vk::PipelineShaderStageCreateInfo vertShaderStageInfo({}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
            //const vk::PipelineShaderStageCreateInfo fragShaderStageInfo({}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main", &numTexturesSpecInfo);

            //const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };


            //auto bindingDescription = vg::VertexPosUvNormal::getBindingDescription();
            //auto attributeDescriptions = vg::VertexPosUvNormal::getAttributeDescriptions();
            //vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 1, &bindingDescription, static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

            //vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

            //vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_context.getWidth()), static_cast<float>(m_context.getHeight()), 0.0f, 1.0f);

            //vk::Rect2D scissor({ 0, 0 }, m_context.getSwapChainExtent());

            //vk::PipelineViewportStateCreateInfo viewportState({}, 1, &viewport, 1, &scissor);

            //vk::PipelineRasterizationStateCreateInfo rasterizer({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);

            //vk::PipelineMultisampleStateCreateInfo mulitsampling({}, vk::SampleCountFlagBits::e1, VK_FALSE);

            //vk::PipelineDepthStencilStateCreateInfo depthStencil({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess);

            //vk::PipelineColorBlendAttachmentState colorBlendAttachment; // standard values for blending.
            //colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            //colorBlendAttachment.setBlendEnable(VK_FALSE);
            //// to enable blending, translate the following into hpp code: (and also use logic op copy in the struct below)
            ////colorBlendAttachment.blendEnable = VK_TRUE;
            ////colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ////colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ////colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            ////colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ////colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ////colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            //vk::PipelineColorBlendStateCreateInfo colorBlending; // standard values for now
            //colorBlending.setLogicOpEnable(VK_FALSE);
            //colorBlending.setLogicOp(vk::LogicOp::eCopy);
            //colorBlending.attachmentCount = 1;
            //colorBlending.setPAttachments(&colorBlendAttachment);
            //colorBlending.setBlendConstants(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

            ////std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };

            ////vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates.size(), dynamicStates.data());

            //std::array<vk::PushConstantRange, 1> vpcr = {
            //    vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, 2 * sizeof(glm::mat4)},
            //};

            //vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            //pipelineLayoutInfo.setSetLayoutCount(1);
            //pipelineLayoutInfo.setPushConstantRangeCount(static_cast<uint32_t>(vpcr.size()));
            //pipelineLayoutInfo.setPPushConstantRanges(vpcr.data());
            //pipelineLayoutInfo.setPSetLayouts(&m_descriptorSetLayout);

            //m_pipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);

            //vk::GraphicsPipelineCreateInfo pipelineInfo;
            //pipelineInfo.stageCount = 2;
            //pipelineInfo.pStages = shaderStages;
            //pipelineInfo.pVertexInputState = &vertexInputInfo;
            //pipelineInfo.pInputAssemblyState = &inputAssembly;
            //pipelineInfo.pViewportState = &viewportState;
            //pipelineInfo.pRasterizationState = &rasterizer;
            //pipelineInfo.pMultisampleState = &mulitsampling;
            //pipelineInfo.pDepthStencilState = &depthStencil;
            //pipelineInfo.pColorBlendState = &colorBlending;
            //pipelineInfo.pDynamicState = nullptr;// &dynamicState;

            //pipelineInfo.layout = m_pipelineLayout;
            //pipelineInfo.renderPass = m_renderpass;
            //pipelineInfo.subpass = 0;   // this is an index
            //// missing: pipeline derivation

            //m_graphicsPipeline = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);

            //m_context.getDevice().destroyShaderModule(vertShaderModule);
            //m_context.getDevice().destroyShaderModule(fragShaderModule);
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

        //void createCommandBuffers()
        //{
        //    vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));

        //    m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

        //    for (size_t i = 0; i < m_commandBuffers.size(); i++)
        //    {
        //        vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);

        //        // begin recording
        //        m_commandBuffers.at(i).begin(beginInfo);

        //        std::array<vk::ClearValue, 2> clearColors = { vk::ClearValue{std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f }}, vk::ClearDepthStencilValue{1.0f, 0} };
        //        vk::RenderPassBeginInfo renderpassInfo(m_renderpass, m_swapChainFramebuffers.at(i), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());

        //        /////////////////////////////
        //        // actual commands start here
        //        /////////////////////////////
        //        m_commandBuffers.at(i).beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

        //        m_commandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

        //        m_commandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
        //        m_commandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint32);

        //        m_commandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, 1, &m_descriptorSets.at(0), 0, nullptr);

        //        m_commandBuffers.at(i).drawIndexedIndirect(m_indirectDrawBufferInfo.m_Buffer, 0, static_cast<uint32_t>(m_scene.getDrawCommandData().size()),
        //            sizeof(std::decay_t<decltype(*m_scene.getDrawCommandData().data())>));

        //        m_commandBuffers.at(i).endRenderPass();

        //        // stop recording
        //        m_commandBuffers.at(i).end();
        //    }
        //}

        //void updatePerFrameInformation(uint32_t currentImage) override
        //{
        //    auto cmdBuf = beginSingleTimeCommands(m_commandPool);

        //    m_camera.update(m_context.getWindow());
        //    if(m_camera.hasChanged())
        //    {
        //        cmdBuf.pushConstants(m_rayTracingPipelineLayout, vk::ShaderStageFlagBits::eRaygenNV, 0, sizeof(glm::mat4), glm::value_ptr(m_camera.getView()));
        //        m_camera.resetChangeFlag();
        //    }

        //    if(m_projectionChanged)
        //    {
        //        cmdBuf.pushConstants(m_rayTracingPipelineLayout, vk::ShaderStageFlagBits::eRaygenNV, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(m_projection));
        //        m_projectionChanged = false;
        //    }

        //    endSingleTimeCommands(cmdBuf, m_context.getGraphicsQueue(), m_commandPool);
        //}

        void configureImgui()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ////// ImGUI WINDOWS GO HERE
            ImGui::ShowDemoWindow();
            m_timer.drawGUIWindow();
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

        //vk::DescriptorSetLayout m_descriptorSetLayout;

        //vk::PipelineLayout m_pipelineLayout;
        //vk::Pipeline m_graphicsPipeline;


        BufferInfo m_vertexBufferInfo;
        BufferInfo m_indexBufferInfo;

        BufferInfo m_offsetBufferInfo;


        //BufferInfo m_indirectDrawBufferInfo;
        //BufferInfo m_modelMatrixBufferInfo;

        //vk::DescriptorPool m_descriptorPool;
        //std::vector<vk::DescriptorSet> m_descriptorSets;


        std::vector<ImageInfo> m_allImages;
        std::vector<vk::ImageView> m_allImageViews;
        std::vector<vk::Sampler> m_allImageSamplers;

        Pilotview m_camera;
        glm::mat4 m_projection;
        bool m_projectionChanged;

        Scene m_scene;

        Timer m_timer;

        // RT stuff
        //BufferInfo m_transformBufferInfo;
        std::vector<ASInfo> m_bottomASs;
        BufferInfo m_instanceBufferInfo;
        ASInfo m_topAS;
        BufferInfo m_scratchBuffer;
        vk::DescriptorSetLayout m_rayTracingDescriptorSetLayout;
        vk::PipelineLayout m_rayTracingPipelineLayout;
        vk::Pipeline m_rayTracingPipeline;
        BufferInfo m_sbtInfo;
        vk::DescriptorPool m_rayTracingDescriptorPool;
        std::vector<vk::DescriptorSet> m_rayTracingDescriptorSets;
        std::vector<vk::CommandBuffer> m_rayTracingCommandBuffers;

/*
        vk::DescriptorSetLayout m_computeDescriptorSetLayout;
        vk::PipelineLayout m_computePipelineLayout;
        vk::Pipeline m_computePipeline;
        vk::DescriptorPool m_computeDescriptorPool;
        std::vector<vk::DescriptorSet> m_computeDescriptorSets;*/
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