#version 460
#extension GL_ARB_separate_shader_objects : enable
const float PI = 3.1415926535;

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

#include "pbrLight.glsl"

layout(set = 2, binding = 0) uniform sampler2DArray shadowDirectionalImage;
layout(set = 2, binding = 1) uniform sampler2DArray shadowPointImage;
layout(set = 2, binding = 2) uniform sampler2DArray shadowSpotImage;
layout(set = 2, binding = 3) uniform sampler2D rtaoImage;
layout(set = 2, binding = 4) uniform sampler2D reflectionImage;

void main() 
{
    vec4 posAndID = texture(gbufferPositionSampler, inUV);
    vec3 WorldPos = posAndID.xyz;
    int drawID = int(posAndID.w);

    // ID -1 means no geometry (background)
    if (drawID == -1) discard;

    // normal in world space
    vec3 N = normalize(texture(gbufferNormalSampler, inUV).xyz); 

    PerMeshInfoPBR currentMeshInfo = perMeshInfos.perMesh[drawID];
    vec4 uvLOD = texture(gbufferUVSampler, inUV);

    MaterialInfoPBR material = materials[currentMeshInfo.assimpMaterialIndex];

    vec3 albedo = vec3(0.0f);
    if(currentMeshInfo.texIndexBaseColor != -1)
        albedo = textureLod(allTextures[currentMeshInfo.texIndexBaseColor], uvLOD.xy, uvLOD.w).xyz;
    else
        albedo = material.baseColor;

    vec3 metallicRoughness = vec3(0.0f);
    if(currentMeshInfo.texIndexMetallicRoughness != -1)
        metallicRoughness = textureLod(allTextures[currentMeshInfo.texIndexMetallicRoughness], uvLOD.xy, uvLOD.w).xyz;
    else
        metallicRoughness = vec3(material.metalness, material.roughness, 0.0f); // what is z?

    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y + 0.01;
        
    ///////////// AO
    float ao = texture(rtaoImage, inUV).x;

    // viewing vector
    vec3 V = normalize(matrices.cameraPos.xyz - WorldPos);

    // reflection vector
    vec3 R = reflect(-V, N);
               
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < dirLights.length(); ++i) 
    {
        float dirShadow = texture(shadowDirectionalImage, vec3(inUV, i)).x;

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
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * dirShadow; 
    }   

    for(int i = 0; i < pointLights.length(); ++i) 
    {
        float pointShadow = texture(shadowPointImage, vec3(inUV, i)).x;

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
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * pointShadow; 
    }

    for(int i = 0; i < spotLights.length(); ++i) 
    {
        float spotShadow = texture(shadowSpotImage, vec3(inUV, i)).x;

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
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * spotShadow; 
    }

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

    vec3 reflectionColor = texture(reflectionImage, inUV).xyz;
    color += reflectionColor;


    // ////////////// TONEMAPPING
    // const float exposure = 1.0f;
    // const float gamma = 2.2f;
    
    // // R E I N H A R D
    // // Exposure tone mapping
    // vec3 mapped = vec3(1.0) - exp(-color * exposure);
    // // Gamma correction 
    // mapped = pow(mapped, vec3(1.0 / gamma));

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  
  
    outColor = vec4(color, 1.0f);

    //outColor = vec4(reflectionColor, 1.0f);

}