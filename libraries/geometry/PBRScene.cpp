#include "PBRScene.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <functional>
#include <glm/gtx/matrix_major_storage.inl>


PBRScene::PBRScene(const std::filesystem::path& filename)
{
	auto logger = spdlog::get("standard");

	auto path = vg::g_resourcesPath / filename;

	logger->info("Loading PBR model from {}", path.string().c_str());

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

	logger->info("Assimp import complete. Processing Model...");

    if (!scene->HasMeshes())
        throw std::runtime_error("No meshes found!");

    const auto numMeshes = scene->mNumMeshes;
	//m_meshes.reserve(numMeshes - 1);

    // process all meshes in the scene
    for (unsigned i = 0; i < numMeshes; i++)
    {
        PerMeshInfoPBR currentMesh;
        currentMesh.instanceCount = 1;

        auto numVertices = scene->mMeshes[i]->mNumVertices;
        
        // save how many vertices are already there to get the starting offset
        currentMesh.vertexOffset = static_cast<int32_t>(m_allVertices.size());
        currentMesh.firstIndex = static_cast<uint32_t>(m_allIndices.size());
        //currentMesh.modelMatrixIndex = i;
        
        // put all vertex positions and uvs in one buffer
        for(unsigned j = 0; j < numVertices; j++)
        {
            vg::VertexPosUvNormal vertex = {};
            vertex.pos = reinterpret_cast<glm::vec3&>(scene->mMeshes[i]->mVertices[j]);
			if (filename.extension() == std::string(".fbx"))
			{
                vertex.normal = glm::vec3(scene->mMeshes[i]->mNormals[j].x, scene->mMeshes[i]->mNormals[j].z, scene->mMeshes[i]->mNormals[j].y);
			}
        	else // gltf
			{
                vertex.normal = reinterpret_cast<glm::vec3&>(scene->mMeshes[i]->mNormals[j]);
			}

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

        currentMesh.assimpMaterialIndex = scene->mMeshes[i]->mMaterialIndex;
        m_meshes.push_back(currentMesh);
    }

	m_modelMatrices = std::vector<glm::mat4>(m_meshes.size(), glm::mat4(1.0f));
	m_allMaterials = std::vector<MaterialInfoPBR>(m_meshes.size(), MaterialInfoPBR());
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
            const glm::mat4 transform = glm::rowMajor4(reinterpret_cast<glm::mat4&>(node->mTransformation));
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



    // import textures

    // maybe throw this out
    if (!scene->HasMaterials())
        throw std::runtime_error("No Materials in PBRScene");

	auto getTexturePaths = [&](const aiMaterial* mat, aiTextureType type, auto& set, auto& vec, const int index)
	{
		if (mat->GetTextureCount(type) > 0)
		{
			aiString reltexPath;
			const auto ret = mat->GetTexture(type, 0, &reltexPath);
			if (ret != AI_SUCCESS) throw std::runtime_error("Texture couldn't be loaded by assimp");

			if(filename.extension() == std::string(".fbx"))
			{
				std::filesystem::path reltexPathAsPath(reltexPath.C_Str());
				std::filesystem::path fn = reltexPathAsPath.filename();
				std::filesystem::path pp = reltexPathAsPath.parent_path().concat("2");
				std::filesystem::path finishedPath = std::filesystem::path(pp) / fn;
				finishedPath.replace_extension(".png");
				reltexPath = finishedPath.string().c_str();
			}

			auto[it, notAlreadyThere] = set.emplace(reltexPath.C_Str());
			if (notAlreadyThere)
			{
				vec.emplace_back(std::make_pair(std::vector<unsigned>( 1, index ), reltexPath.C_Str()));
			}
			else // find the texture if it already exists, and append its materialindex
			{
				auto prevTexIt = std::find_if(vec.begin(), vec.end(),
					[&reltexPath](const auto& bla) { return std::strcmp(bla.second.c_str(), reltexPath.C_Str()) == 0; });
				if (prevTexIt == vec.end()) throw std::runtime_error("Expected to find texture path, but none found.");
				prevTexIt->first.push_back(index);
			}
		}
	};

    for(unsigned i = 0; i < scene->mNumMaterials; i++)
    {
        const auto mat = scene->mMaterials[i];
        // todo other types

		if(filename.extension() == std::string(".gltf"))
		{
			for (aiTextureType type : {aiTextureType_DIFFUSE, aiTextureType_UNKNOWN}) // unknown = metallic roughness in this case
			{
				switch (type)
				{
				case aiTextureType_DIFFUSE:
					getTexturePaths(mat, type, m_texturesBaseColorPathSet, m_indexedBaseColorTexturePaths, i);
					break;
				case aiTextureType_UNKNOWN:
					getTexturePaths(mat, type, m_texturesMetallicRoughnessPathSet, m_indexedMetallicRoughnessTexturePaths, i);
					break;
				default:
					break;
				}
			}

			aiColor3D diffColor;
			auto ret = mat->Get("$mat.gltf.pbrMetallicRoughness.baseColorFactor", 0, 0, diffColor);
			if (ret != AI_SUCCESS) throw std::runtime_error("Failure in Material");
			m_allMaterials.at(i).baseColor = reinterpret_cast<glm::vec3&>(diffColor);

			ret = mat->Get("$mat.gltf.pbrMetallicRoughness.metallicFactor", 0, 0, m_allMaterials.at(i).metalness);
			if (ret != AI_SUCCESS) throw std::runtime_error("Failure in Material");

			ret = mat->Get("$mat.gltf.pbrMetallicRoughness.roughnessFactor", 0, 0, m_allMaterials.at(i).roughness);
			if (ret != AI_SUCCESS) throw std::runtime_error("Failure in Material");
		}
		else if(filename.extension() == std::string(".fbx"))
		{
			for (aiTextureType type : {aiTextureType_DIFFUSE, aiTextureType_SPECULAR}) // unknown = metallic roughness in this case
			{
				switch (type)
				{
				case aiTextureType_DIFFUSE:
					getTexturePaths(mat, type, m_texturesBaseColorPathSet, m_indexedBaseColorTexturePaths, i);
					break;
				case aiTextureType_SPECULAR:
					getTexturePaths(mat, type, m_texturesMetallicRoughnessPathSet, m_indexedMetallicRoughnessTexturePaths, i);
					break;
				default:
					break;
				}
			}

			aiColor3D diffColor;
			auto ret = mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffColor);
			if (ret != AI_SUCCESS) throw std::runtime_error("Failure in Material");
			m_allMaterials.at(i).baseColor = reinterpret_cast<glm::vec3&>(diffColor);

			aiColor3D specColor;
			mat->Get(AI_MATKEY_COLOR_SPECULAR, specColor);
			// 0 is ao, not used
			m_allMaterials.at(i).roughness = specColor[1];
			m_allMaterials.at(i).metalness = specColor[2];
		}
		else
		{
			throw std::runtime_error("Non-supported format");
		}

		if(m_allMaterials.at(i).metalness > 0.5)
			m_allMaterials.at(i).f0 = glm::vec3(0.91f, 0.92f, 0.92f); // f0: aluminium, gltf does not contain anything
		else
			m_allMaterials.at(i).f0 = glm::vec3(0.04f, 0.04f, 0.04f); // f0: dielectrics
        
    }




    int uniqueTexIndex = 0;
    for(const auto& indexTexPair : m_indexedBaseColorTexturePaths)
    {
        for (unsigned index : indexTexPair.first)
            for (auto& mesh : m_meshes)
                if (mesh.assimpMaterialIndex == index)
                    mesh.texIndexBaseColor = uniqueTexIndex;

        uniqueTexIndex++;
    }

	int uniqueSpecTexIndex = 0;
	for (const auto& indexTexPair : m_indexedMetallicRoughnessTexturePaths)
	{
		for (unsigned index : indexTexPair.first)
			for (auto& mesh : m_meshes)
				if (mesh.assimpMaterialIndex == index)
					mesh.texIndexMetallicRoughness = uniqueSpecTexIndex + static_cast<int32_t>(m_indexedBaseColorTexturePaths.size());

		uniqueSpecTexIndex++;
	}


    
    importer.FreeScene();

	logger->info("Geometry Processing complete");

}
