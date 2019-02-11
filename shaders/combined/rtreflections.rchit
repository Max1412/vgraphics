#version 460
#extension GL_NV_ray_tracing : require

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

struct MaterialInfo
{
    vec3 diffColor;
    vec3 specColor;
    float N;
};

layout(set = 0, binding = 9) readonly buffer materialBuffer
{
    MaterialInfo materials[];
};

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

layout(std430, set = 0, binding = 10) readonly buffer indirectDrawBuffer
{
    PerMeshInfo perMesh[];
} perMeshInfos;

#include "structs.glsl"

layout(set = 0, binding = 4) readonly buffer rtPerFrameBuffer
{
    RTperFrameInfo2 perFrameInfo;
};

#include "light.glsl"

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
    vec3 pos = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;
    vec3 normal = normalize(barycentrics.x * vertex0.normal + barycentrics.y * vertex1.normal + barycentrics.z * vertex2.normal);

    PerMeshInfo currentMeshInfo = perMeshInfos.perMesh[gl_InstanceCustomIndexNV];
    MaterialInfo material = materials[currentMeshInfo.assimpMaterialIndex];

    
    vec3 diffCol = vec3(0.0f);
    if(currentMeshInfo.texIndex != -1)
        diffCol = texture(allTextures[currentMeshInfo.texIndex], uv).xyz;
    else
        diffCol = material.diffColor;

    vec3 specCol = vec3(0.0f);
    if(currentMeshInfo.texSpecIndex != -1)
        specCol = texture(allTextures[currentMeshInfo.texSpecIndex], uv).xyz;
    else
        specCol = material.specColor;

    ///////////// LIGHTING
    const vec3 ambient = vec3(0.3f);
    vec3 lightingColor = ambient * diffCol;


    LightResult result;
    vec3 viewDir = normalize(perFrameInfo.cameraPosWorld - pos);
    for(int i = 0; i < dirLights.length(); i++)
    {
        DirectionalLight currentLight = dirLights[i];
        vec3 lightDir = normalize(-currentLight.direction);
        
        traceNV(topLevelAS, rayFlags, cullMask,
                1 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 1 /*missIndex*/,
                pos, 0.001, lightDir, 100000.0,
                1 /*payload*/ // X here is location = X of the payload
        );

        if(rtSecondaryShadow == 0) continue;
        float dirShadow = rtSecondaryShadow;

        normal = normal == vec3(0.0f) ? lightDir : normal;

        // diffuse shading
        float diff = max(dot(normal, lightDir), 0.0);

        // specular shading
        vec3 halfwayDir = normalize(lightDir + viewDir);  
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.N);

        // combine results
        result.diffuse = currentLight.intensity * diff;
        result.specular = currentLight.intensity * spec;
        result.direction = lightDir;

        lightingColor += dirShadow * (diffCol * result.diffuse + specCol * result.specular);
        
    }

    for(int i = 0; i < pointLights.length(); i++)
    {
        PointLight currentLight = pointLights[i];
        vec3 lightDir = normalize(currentLight.position - pos);

        traceNV(topLevelAS, rayFlags, cullMask,
                1 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 1 /*missIndex*/,
                pos, 0.001, lightDir, length(currentLight.position - pos),
                1 /*payload*/ // X here is location = X of the payload
        );

        if(rtSecondaryShadow == 0) continue;
        float pointShadow = rtSecondaryShadow;

        normal = normal == vec3(0.0f) ? lightDir : normal;

        // diffuse shading
        float diff = max(dot(normal, lightDir), 0.0);

        // specular shading
        vec3 halfwayDir = normalize(lightDir + viewDir);  
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.N);

        // attenuation
        float distance = length(currentLight.position - pos);
        float attenuation = 1.0 / max(0.001f, (currentLight.constant + currentLight.linear * distance + currentLight.quadratic * (distance * distance)));

        // combine results
        result.diffuse = currentLight.intensity * diff * attenuation;
        result.specular = currentLight.intensity * spec * attenuation;
        result.direction = lightDir;

        lightingColor += pointShadow * (diffCol * result.diffuse + specCol * result.specular);
    }

    for(int i = 0; i < spotLights.length(); i++)
    {
        SpotLight currentLight = spotLights[i];
        vec3 lightDir = normalize(currentLight.position - pos);

        traceNV(topLevelAS, rayFlags, cullMask,
                1 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 1 /*missIndex*/,
                pos, 0.001, lightDir, length(currentLight.position - pos),
                1 /*payload*/ // X here is location = X of the payload
        );

        if(rtSecondaryShadow == 0) continue;
        float spotShadow = rtSecondaryShadow;


        normal = normal == vec3(0.0f) ? lightDir : normal;

        // diffuse shading
        float diff = max(dot(normal, lightDir), 0.0);

        // specular shading
        vec3 halfwayDir = normalize(lightDir + viewDir);  
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.N);

        // attenuation
        float distance = length(currentLight.position - pos);
        float attenuation = 1.0 / max(0.001f, (currentLight.constant + currentLight.linear * distance + currentLight.quadratic * (distance * distance)));

        // spotlight intensity
        float theta = dot(lightDir, normalize(-currentLight.direction));
        float epsilon = currentLight.cutoff - currentLight.outerCutoff;
        float intensity = clamp((theta - currentLight.outerCutoff) / epsilon, 0.0, 1.0);

        // combine results
        result.diffuse = currentLight.intensity * diff * attenuation * intensity;
        result.specular = currentLight.intensity * spec * attenuation * intensity;
        result.direction = lightDir;

        lightingColor += spotShadow * (diffCol * result.diffuse + specCol * result.specular);
    }

    hitValue = lightingColor;

}