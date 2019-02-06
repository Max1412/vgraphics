#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in int drawID;
layout(location = 2) in vec3 passNormal;
layout(location = 3) in vec3 passWorldPos;

layout(location = 0) out vec4 gbufferPosition;
layout(location = 1) out vec4 gbufferNormal;
layout(location = 2) out vec4 gbufferUV;
layout(location = 3) out int gbufferMeshID;

layout(constant_id = 0) const int NUM_TEXTURES = 64;
layout(set = 0, binding = 1) uniform sampler2D allTextures[NUM_TEXTURES];

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

layout(std430, set = 0, binding = 2) readonly buffer indirectDrawBuffer
{
    PerMeshInfo perMesh[];
} perMeshInfos;

void main()
{
    gbufferPosition = vec4(passWorldPos, drawID);
    gbufferNormal = vec4(passNormal, 0.0f);
    vec2 lod = textureQueryLod(allTextures[perMeshInfos.perMesh[drawID].texIndex], fragTexCoord);
    gbufferUV = vec4(fragTexCoord, lod.x, lod.y);
    gbufferMeshID = drawID;
}