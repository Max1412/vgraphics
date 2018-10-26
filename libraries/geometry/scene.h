#pragma once

#include <filesystem>
#include "glm/glm.hpp"
#include <vulkan/vulkan.hpp>
#include "graphic/Definitions.h"

using Mesh = vk::DrawIndexedIndirectCommand;

//struct Mesh
//{
//    vk::DeviceSize numVertices = 0;
//    vk::DeviceSize numIndices = 0;
//    vk::DeviceSize vertexOffset = 0;
//    vk::DeviceSize indexOffset = 0;
//    vk::DeviceSize modelMatrixIndex = 0;
//};


class Scene
{
public:
    explicit Scene(const std::filesystem::path& scenePath);

    const std::vector<vg::VertexPosUv>& getVertices() const { return m_allVertices; }
    const std::vector<uint32_t>& getIndices() const { return m_allIndices; }
    const std::vector<Mesh>& getDrawCommandData() const { return m_meshes; }
    const std::vector<glm::mat4>& getModelMatrices() const { return m_modelMatrices; }

private:
    std::vector<uint32_t> m_allIndices;
    std::vector<vg::VertexPosUv> m_allVertices;
    std::vector<Mesh> m_meshes;
    std::vector<glm::mat4> m_modelMatrices;
};
