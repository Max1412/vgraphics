struct DirectionalLight
{
    vec3 direction;
    int numShadowSamples;
    vec3 intensity;
};

struct PointLight
{
    vec3 position;
    float constant;
    vec3 intensity;
    float linear;
    float quadratic;
    float radius;
    int numShadowSamples;
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
    float radius;
    int numShadowSamples;
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

bool checkDirectionalShadow(in int lightID, in uvec4 bitfield)
{
    return bitfieldExtract(bitfield.x, lightID, 1) == 1;
}