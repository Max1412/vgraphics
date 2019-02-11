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
layout(set = 0, binding = 4) uniform sampler2D gbufferUVSampler;

struct MaterialInfo
{
    vec3 diffColor;
    vec3 specColor;
    float N;
};

layout(set = 0, binding = 5) readonly buffer materialBuffer
{
    MaterialInfo materials[];
};

layout (push_constant) uniform perFramePush
{
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} matrices;

#include "light.glsl"

layout(set = 2, binding = 0) uniform sampler2DArray shadowDirectionalImage;
layout(set = 2, binding = 1) uniform sampler2DArray shadowPointImage;
layout(set = 2, binding = 2) uniform sampler2DArray shadowSpotImage;
layout(set = 2, binding = 3) uniform sampler2D rtaoImage;
layout(set = 2, binding = 4) uniform sampler2D reflectionImage;

void main() 
{
    vec4 posAndID = texture(gbufferPositionSampler, inUV);
    vec3 pos = posAndID.xyz;
    int drawID = int(posAndID.w);

    // ID -1 means no geometry (background)
    if (drawID == -1) discard;

    vec3 normal = normalize(texture(gbufferNormalSampler, inUV).xyz);
    PerMeshInfo currentMeshInfo = perMeshInfos.perMesh[drawID];
    vec4 uvLOD = texture(gbufferUVSampler, inUV);

    MaterialInfo material = materials[currentMeshInfo.assimpMaterialIndex];

    vec3 diffCol = vec3(0.0f);
    if(currentMeshInfo.texIndex != -1)
        diffCol = textureLod(allTextures[currentMeshInfo.texIndex], uvLOD.xy, uvLOD.w).xyz;
    else
        diffCol = material.diffColor;

    vec3 specCol = vec3(0.0f);
    if(currentMeshInfo.texSpecIndex != -1)
        specCol = textureLod(allTextures[currentMeshInfo.texSpecIndex], uvLOD.xy, uvLOD.w).xyz;
    else
        specCol = material.specColor;
    
        
    ///////////// AO
    float AO = texture(rtaoImage, inUV).x;

    ///////////// LIGHTING
    const vec3 ambient = vec3(0.3f);
    vec3 lightingColor = ambient * diffCol;


    LightResult result;
    vec3 viewDir = normalize(matrices.cameraPos.xyz - pos);
    for(int i = 0; i < dirLights.length(); i++)
    {
        float dirShadow = texture(shadowDirectionalImage, vec3(inUV.xy, i)).x;

        DirectionalLight currentLight = dirLights[i];
        vec3 lightDir = normalize(-currentLight.direction);
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
        float pointShadow = texture(shadowPointImage, vec3(inUV.xy, i)).x;

        if(pointShadow < 0.001) continue;

        PointLight currentLight = pointLights[i];

        vec3 lightDir = normalize(currentLight.position - pos);
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
        float spotShadow = texture(shadowSpotImage, vec3(inUV.xy, i)).x;

        SpotLight currentLight = spotLights[i];

        vec3 lightDir = normalize(currentLight.position - pos);
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

    vec3 reflectionColor = texture(reflectionImage, inUV).xyz;
    lightingColor += reflectionColor;

    lightingColor *= AO;

    ////////////// TONEMAPPING
    vec3 hdrColor = lightingColor;

    const float exposure = 0.1f;
    const float gamma = 2.2f;
    
    // R E I N H A R D
    // Exposure tone mapping
    vec3 mapped = vec3(1.0) - exp(-hdrColor * exposure);
    // Gamma correction 
    mapped = pow(mapped, vec3(1.0 / gamma));
  
    outColor = vec4(mapped, 1.0f);

    //outColor = vec4(reflectionColor, 1.0f);
}