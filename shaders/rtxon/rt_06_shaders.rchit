#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec2 attribs;

layout(constant_id = 0) const int NUM_TEXTURES = 64;
layout(set = 0, binding = 2) uniform sampler2D allTextures[NUM_TEXTURES];

struct VertexInfo
{
    vec3 pos;
    vec2 uv;
    vec3 normal;
};

layout(std430, set = 0, binding = 3) readonly buffer vertexBuffer
{
    VertexInfo vertices[];
} vertexInfos;

layout(std430, set = 0, binding = 4) readonly buffer indexBuffer
{
    uint indices[];
} indexInfos;


struct OffsetInfo
{
    int m_vbOffset;
    int m_ibOffset;
    int m_diffTextureID;
    int m_specTextureID;
};

layout(std430, set = 0, binding = 5) readonly buffer offsetBuffer
{
    OffsetInfo offsets[];
} offsetInfos;

void main()
{
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentrics;
    // if(gl_InstanceCustomIndexNV < 3)
    // {
    //     hitValue = texture(allTextures[nonuniformEXT(gl_InstanceCustomIndexNV)], attribs).rgb;
    // }

    OffsetInfo currentOffset = offsetInfos.offsets[gl_InstanceCustomIndexNV];

    uint index0 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 0)];
    uint index1 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 1)];
    uint index2 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 2)];

    VertexInfo vertex0 = vertexInfos.vertices[currentOffset.m_vbOffset + index0];
    VertexInfo vertex1 = vertexInfos.vertices[currentOffset.m_vbOffset + index1];
    VertexInfo vertex2 = vertexInfos.vertices[currentOffset.m_vbOffset + index2];

    const vec2 texcoords = barycentrics.x * vertex0.uv + barycentrics.y * vertex1.uv + barycentrics.z * vertex2.uv;

    if(currentOffset.m_diffTextureID != -1)
        hitValue = texture(allTextures[currentOffset.m_diffTextureID], texcoords).rgb;
    else
        hitValue = vec3(0.5, 0.8, 0.2);
}