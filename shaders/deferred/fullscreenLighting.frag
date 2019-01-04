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

struct DirectionalLight
{
    vec3 direction;
    vec3 intensity;
};

struct PointLight
{
    vec3 position;
    float constant;
    vec3 intensity;
    float linear;
    float quadratic;
};

struct SpotLight
{
    vec3 position;
    float constant;
    vec3 intensity;
    float linear;
    vec3 direction;
    float quadratic;
    float cutoff;
    float outerCutoff;
};

layout(set = 1, binding = 0) readonly buffer dirLightBuffer
{
    DirectionalLight dirLights[];
};

layout(set = 1, binding = 1) readonly buffer pointLightBuffer
{
    PointLight pointLights[];
};

layout(set = 1, binding = 2) readonly buffer spotLightBuffer
{
    SpotLight spotLights[];
};

struct LightResult
{
    vec3 diffuse;
    vec3 specular;
    vec3 direction;
};



void main() 
{
    //TODO do shading here
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
    

    ///////////// LIGHTING
    const vec3 ambient = vec3(0.3f);
    vec3 lightingColor = ambient * diffCol;

    LightResult result;
    vec3 viewDir = normalize(matrices.cameraPos.xyz - pos);
    for(int i = 0; i < dirLights.length(); i++)
    {
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

    }
    lightingColor += (diffCol * result.diffuse + specCol * result.specular);

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
}