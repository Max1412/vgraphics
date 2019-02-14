#version 460
#extension GL_NV_ray_tracing : require
const float PI = 3.1415926535;

layout(set = 0, binding = 0) uniform accelerationStructureNV topLevelAS;

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec2 attribs;
layout(location = 1) rayPayloadNV int rtSecondaryShadow;

layout(constant_id = 0) const int NUM_TEXTURES = 64;
layout(set = 0, binding = 11) uniform sampler2D allTextures[NUM_TEXTURES];

struct VertexInfo
{
    vec3 pos;
    vec2 uv;
    vec3 normal;
};

layout(std430, set = 0, binding = 6) readonly buffer vertexBuffer
{
    VertexInfo vertices[];
} vertexInfos;

layout(std430, set = 0, binding = 7) readonly buffer indexBuffer
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

layout(std430, set = 0, binding = 8) readonly buffer offsetBuffer
{
    OffsetInfo offsets[];
} offsetInfos;


struct MaterialInfoPBR
{
	vec3 baseColor;
	float roughness;
	vec3 f0;
	float metalness;
};

layout(set = 0, binding = 9) readonly buffer materialBuffer
{
    MaterialInfoPBR materials[];
};

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

layout(std430, set = 0, binding = 10) readonly buffer indirectDrawBuffer
{
    PerMeshInfoPBR perMesh[];
} perMeshInfos;

#include "structs.glsl"

layout(set = 0, binding = 4) readonly buffer rtPerFrameBuffer
{
    RTperFrameInfo2 perFrameInfo;
};

#include "pbrLight.glsl"

void main()
{
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    //hitValue = barycentrics;

    OffsetInfo currentOffset = offsetInfos.offsets[gl_InstanceCustomIndexNV];

    uint index0 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 0)];
    uint index1 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 1)];
    uint index2 = indexInfos.indices[currentOffset.m_ibOffset + (3 * gl_PrimitiveID + 2)];

    VertexInfo vertex0 = vertexInfos.vertices[currentOffset.m_vbOffset + index0];
    VertexInfo vertex1 = vertexInfos.vertices[currentOffset.m_vbOffset + index1];
    VertexInfo vertex2 = vertexInfos.vertices[currentOffset.m_vbOffset + index2];

    const vec2 uv = barycentrics.x * vertex0.uv + barycentrics.y * vertex1.uv + barycentrics.z * vertex2.uv;

    // ray stuff
    uint rayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsTerminateOnFirstHitNV;
    uint cullMask = 0xff;

    // LIGHTING SHADER STARTS HERE --- KEEP UP TO DATE
    vec3 WorldPos = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;
    vec3 N = normalize(barycentrics.x * vertex0.normal + barycentrics.y * vertex1.normal + barycentrics.z * vertex2.normal);

    PerMeshInfoPBR currentMeshInfo = perMeshInfos.perMesh[gl_InstanceCustomIndexNV];
    MaterialInfoPBR material = materials[currentMeshInfo.assimpMaterialIndex];

    
    vec3 albedo = vec3(0.0f);
    if(currentMeshInfo.texIndexBaseColor != -1)
        albedo = texture(allTextures[currentMeshInfo.texIndexBaseColor], uv).xyz;
    else
        albedo = material.baseColor;;

    vec3 metallicRoughness = vec3(0.0f);
    if(currentMeshInfo.texIndexMetallicRoughness != -1)
        metallicRoughness = texture(allTextures[currentMeshInfo.texIndexMetallicRoughness], uv).xyz;
    else
        metallicRoughness = vec3(material.metalness, material.roughness, 0.0f); // what is z?

    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y + 0.01;

   // viewing vector
    vec3 V = normalize(perFrameInfo.cameraPosWorld - WorldPos);

    // reflection vector
    vec3 R = reflect(-V, N);
               
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < dirLights.length(); ++i) //TODO trace reflection rays!!
    {
        // get light parameters
        PBRDirectionalLight currentLight = dirLights[i];

        // light vector
        vec3 L = normalize(-currentLight.direction);

        // halfway vector
        vec3 H = normalize(V + L);

        // dir. lights don't have a falloff
        vec3 radiance = currentLight.intensity;  
        
        // calculate the parts of the Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), material.f0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        // Cook-Torrance specular BRDF term: DFG / 4(w0 . n)(wi . n)
        vec3 nominator    = NDF * G * F;
        float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3 specular     = nominator / denominator;
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }   

    for(int i = 0; i < pointLights.length(); ++i) 
    {
        // get light parameters
        PBRPointLight currentLight = pointLights[i];

        // light vector
        vec3 L = normalize(currentLight.position - WorldPos);

        // halfway vector
        vec3 H = normalize(V + L);

        // calculate per-light radiance
        float distance    = length(currentLight.position - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = currentLight.intensity * attenuation;        
        
        // calculate the parts of the Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), material.f0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        // Cook-Torrance specular BRDF term: DFG / 4(w0 . n)(wi . n)
        vec3 nominator    = NDF * G * F;
        float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3 specular     = nominator / denominator;
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }

    for(int i = 0; i < spotLights.length(); ++i) 
    {
        // get light parameters
        PBRSpotLight currentLight = spotLights[i];

        // light vector
        vec3 L = normalize(currentLight.position - WorldPos);

        // halfway vector
        vec3 H = normalize(V + L);

        // calculate per-light radiance
        float distance    = length(currentLight.position - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = currentLight.intensity * attenuation; 

        // spotlight intensity (falloff to sides of spot)
        float theta = dot(L, normalize(-currentLight.direction));
        float epsilon = currentLight.cutoff - currentLight.outerCutoff;
        radiance *= clamp((theta - currentLight.outerCutoff) / epsilon, 0.0, 1.0);       
        
        // calculate the parts of the Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), material.f0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        // Cook-Torrance specular BRDF term: DFG / 4(w0 . n)(wi . n)
        vec3 nominator    = NDF * G * F;
        float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3 specular     = nominator / denominator;
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }

    vec3 ambient = vec3(0.03) * albedo; // no AO here
    vec3 color = ambient + Lo;

    hitValue = color;

}