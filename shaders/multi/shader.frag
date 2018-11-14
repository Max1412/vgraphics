#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable 

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in int drawID;
layout(location = 2) in vec3 passNormal;

layout(location = 0) out vec4 outColor;

struct PerMeshInfo
{
    // standard
    uint    indexCount;
    uint    instanceCount;
    uint    firstIndex;
    int     vertexOffset;
    uint    firstInstance;
    // additional
    int texIndex;
    int assimpMaterialIndex;
};

layout(std430, set = 0, binding = 1) readonly buffer indirectDrawBuffer
{
    PerMeshInfo perMesh[];
} perMeshInfos;

layout(constant_id = 0) const int NUM_TEXTURES = 64;

layout(set = 0, binding = 2) uniform sampler2D texSampler;

layout(set = 0, binding = 3) uniform sampler2D allTextures[NUM_TEXTURES];

void main()
{
    outColor = texture(allTextures[perMeshInfos.perMesh[drawID].texIndex], fragTexCoord);
    //outColor += vec4(passNormal, 1.0);
}