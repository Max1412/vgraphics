#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

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
    int texSpecIndex;
    int assimpMaterialIndex;
};

layout(std430, set = 0, binding = 0) readonly buffer indirectDrawBuffer
{
    PerMeshInfo perMesh[];
} perMeshInfos;

layout(constant_id = 0) const int NUM_TEXTURES = 64;
layout(set = 0, binding = 1) uniform sampler2D allTextures[NUM_TEXTURES];
layout(set = 0, binding = 2) uniform sampler2D gbufferPositionSampler;
layout(set = 0, binding = 3) uniform sampler2D gbufferNormalSampler;
layout(set = 0, binding = 3) uniform sampler2D gbufferUVSampler;


void main() 
{
    //TODO do shading here
    //outColor = texture(gbufferPositionSampler, inUV);
    outColor = vec4(texture(gbufferUVSampler, inUV).xy, 0.0, 1.0);
}