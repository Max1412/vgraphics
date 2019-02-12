#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

struct PerMeshInfoPBR
{
    // standard
    uint    indexCount;
    uint    instanceCount;
    uint    firstIndex;
    int     vertexOffset;
    uint    firstInstance;
    // additional
	int texIndexBaseColor;
	int texIndexMetallicRoughness;
    int assimpMaterialIndex;
};

layout(std430, set = 0, binding = 0) readonly buffer indirectDrawBuffer
{
    PerMeshInfoPBR perMesh[];
} perMeshInfos;

layout(constant_id = 0) const int NUM_TEXTURES = 64;
layout(set = 0, binding = 1) uniform sampler2D allTextures[NUM_TEXTURES];
layout(set = 0, binding = 2) uniform sampler2D gbufferPositionSampler;
layout(set = 0, binding = 3) uniform sampler2D gbufferNormalSampler;
layout(set = 0, binding = 4) uniform sampler2D gbufferUVSampler;

struct MaterialInfoPBR
{
	vec3 baseColor;
	float roughness;
	vec3 f0;
	float metalness;
};

layout(set = 0, binding = 5) readonly buffer materialBuffer
{
    MaterialInfoPBR materials[];
};

layout (push_constant) uniform perFramePush
{
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} matrices;

#include "light.glsl"

void main() 
{
    vec4 posAndID = texture(gbufferPositionSampler, inUV);
    vec3 pos = posAndID.xyz;
    int drawID = int(posAndID.w);

    // ID -1 means no geometry (background)
    if (drawID == -1) discard;

    vec3 normal = normalize(texture(gbufferNormalSampler, inUV).xyz);
    PerMeshInfoPBR currentMeshInfo = perMeshInfos.perMesh[drawID];
    vec4 uvLOD = texture(gbufferUVSampler, inUV);

    MaterialInfoPBR material = materials[currentMeshInfo.assimpMaterialIndex];

    vec3 baseColor = vec3(0.0f);
    if(currentMeshInfo.texIndexBaseColor != -1)
        baseColor = textureLod(allTextures[currentMeshInfo.texIndexBaseColor], uvLOD.xy, uvLOD.w).xyz;
    else
        baseColor = material.baseColor;

    vec3 metallicRoughness = vec3(0.0f);
    if(currentMeshInfo.texIndexMetallicRoughness != -1)
        metallicRoughness = textureLod(allTextures[currentMeshInfo.texIndexMetallicRoughness], uvLOD.xy, uvLOD.w).xyz;
    else
        metallicRoughness = vec3(material.roughness, material.metalness, 0.0); // is this correct? check with sketchfab

    //what is z?




    // ////////////// TONEMAPPING
    // vec3 hdrColor = lightingColor;

    // const float exposure = 0.1f;
    // const float gamma = 2.2f;
    
    // // R E I N H A R D
    // // Exposure tone mapping
    // vec3 mapped = vec3(1.0) - exp(-hdrColor * exposure);
    // // Gamma correction 
    // mapped = pow(mapped, vec3(1.0 / gamma));
  
    outColor = vec4(metallicRoughness.w);
}