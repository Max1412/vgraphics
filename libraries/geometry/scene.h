#pragma once

#include <filesystem>
#include "glm/glm.hpp"
#include <vulkan/vulkan.hpp>
#include "graphic/Definitions.h"
#include <map>
#include <set>
#include "graphic/BaseApp.h"


//struct Mesh
//{
//    vk::DeviceSize numVertices = 0;
//    vk::DeviceSize numIndices = 0;
//    vk::DeviceSize vertexOffset = 0;
//    vk::DeviceSize indexOffset = 0;
//    vk::DeviceSize modelMatrixIndex = 0;
//};

struct PerMeshInfo : vk::DrawIndexedIndirectCommand
{
    int texIndex = -1;
	int texSpecIndex = -1;
    int assimpMaterialIndex = -1;
};


class Scene
{
public:
    explicit Scene(const std::filesystem::path& filename);

    std::vector<vg::VertexPosUvNormal>& getVertices() { return m_allVertices; }
    const std::vector<uint32_t>& getIndices() const { return m_allIndices; }
    const std::vector<PerMeshInfo>& getDrawCommandData() const { return m_meshes; }
    const std::vector<glm::mat4>& getModelMatrices() const { return m_modelMatrices; }
    const std::vector<std::pair<std::vector<unsigned>, std::string>>& getIndexedDiffuseTexturePaths() const { return m_indexedDiffuseTexturePaths;  }
	const std::vector<std::pair<std::vector<unsigned>, std::string>>& getIndexedSpecularTexturePaths() const { return m_indexedSpecularTexturePaths; }

private:
    std::vector<uint32_t> m_allIndices;
    std::vector<vg::VertexPosUvNormal> m_allVertices;
    std::vector<PerMeshInfo> m_meshes;
    std::vector<glm::mat4> m_modelMatrices;

    std::set<std::string> m_texturesDiffusePathSet;
	std::set<std::string> m_texturesSpecularPathSet;

    std::vector<std::pair<std::vector<unsigned>, std::string>> m_indexedDiffuseTexturePaths;
	std::vector<std::pair<std::vector<unsigned>, std::string>> m_indexedSpecularTexturePaths;

};
