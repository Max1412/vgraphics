#include "scene.h"
#include <iostream>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <functional>


Scene::Scene(const std::filesystem::path& filename)
{
    std::cout << "Loading model from " << filename.string() << std::endl;
    auto path = vg::g_resourcesPath / filename;
    Assimp::Importer importer;

    auto scene = importer.ReadFile(path.string().c_str(), aiProcess_GenSmoothNormals |
        aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_JoinIdenticalVertices //  |
        //aiProcess_RemoveComponent |
        //    aiComponent_ANIMATIONS |
        //    aiComponent_BONEWEIGHTS |
        //    aiComponent_CAMERAS |
        //    aiComponent_LIGHTS |
        //    // aiComponent_TANGENTS_AND_BITANGENTS |
        //    aiComponent_COLORS |
        //aiProcess_SplitLargeMeshes |
        //aiProcess_ImproveCacheLocality |
        //aiProcess_RemoveRedundantMaterials |
        //aiProcess_OptimizeMeshes |
        //aiProcess_SortByPType |
        //aiProcess_FindDegenerates |
        //aiProcess_FindInvalidData
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
    {
        const std::string err = importer.GetErrorString();
        throw std::runtime_error("Assimp import failed: " + err);
    }

    std::cout << "Assimp import complete. Processing Model..." << std::endl;

    if (!scene->HasMeshes())
        throw std::runtime_error("No meshes found!");

    const auto numMeshes = scene->mNumMeshes;
    m_meshes.reserve(numMeshes);
    m_modelMatrices = std::vector<glm::mat4>(numMeshes, glm::mat4(1.0f));

    // process all meshes in the scene
    for (unsigned i = 0; i < numMeshes; i++)
    {
        vk::DrawIndexedIndirectCommand currentMesh;
        currentMesh.instanceCount = 1;

        auto numVertices = scene->mMeshes[i]->mNumVertices;
        
        // save how many vertices are already there to get the starting offset
        currentMesh.vertexOffset = static_cast<int32_t>(m_allVertices.size());
        currentMesh.firstIndex = static_cast<uint32_t>(m_allIndices.size());
        //currentMesh.modelMatrixIndex = i;
        
        // put all vertex positions and uvs in one buffer
        for(unsigned j = 0; j < numVertices; j++)
        {
            vg::VertexPosUv vertex = {};
            vertex.pos = reinterpret_cast<glm::vec3&>(scene->mMeshes[i]->mVertices[j]);

            if (scene->mMeshes[i]->HasTextureCoords(0))
            {
                vertex.uv = glm::vec2(reinterpret_cast<glm::vec3&>(scene->mMeshes[i]->mTextureCoords[0][j]));
            }
            else
            {
                vertex.uv = glm::vec2(0.0f);
            }

            m_allVertices.push_back(vertex);
        }

        // put all indices in one buffer
        for (unsigned int n = 0; n < scene->mMeshes[i]->mNumFaces; n++)
        {
            const auto face = scene->mMeshes[i]->mFaces[n];
            for (unsigned int m = 0; m < face.mNumIndices; m++)
            {
                m_allIndices.push_back(face.mIndices[m]);
            }
        }

        currentMesh.indexCount = static_cast<uint32_t>(m_allIndices.size()) - currentMesh.firstIndex;

        m_meshes.push_back(currentMesh);
    }

    // accumulate all hierarchical transformations to model matrices
    const auto root = scene->mRootNode;
    const glm::mat4 startTransform(1.0f);

    static_assert(alignof(aiMatrix4x4) == alignof(glm::mat4) && sizeof(aiMatrix4x4) == sizeof(glm::mat4));

    std::function<void(aiNode* node, glm::mat4 trans)> traverseChildren = [this, &traverseChildren](aiNode* node, glm::mat4 trans)
    {
        // check if transformation exists
        if (std::none_of(&node->mTransformation.a1, (&node->mTransformation.d4) + 1,
            [](float f) { return std::isnan(f) || std::isinf(f); }))
        {
            // accumulate transform
            const glm::mat4 transform = reinterpret_cast<glm::mat4&>(node->mTransformation);
            trans *= transform;
        }

        // assign transformation to meshes
        #pragma omp parallel for
        for (int i = 0; i < static_cast<int>(node->mNumMeshes); ++i)
        {
            //m_meshes.at(node->mMeshes[i])->setModelMatrix(trans);
            m_modelMatrices.at(node->mMeshes[i]) = trans;
        }

        // recursively work on the child nodes
        #pragma omp parallel for
        for (int i = 0; i < static_cast<int>(node->mNumChildren); ++i)
        {
            traverseChildren(node->mChildren[i], trans);
        }
    };

    traverseChildren(root, startTransform);

    importer.FreeScene();
}
