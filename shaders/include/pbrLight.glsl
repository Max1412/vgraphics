struct PBRDirectionalLight
{
    vec3 direction;
    int numShadowSamples;
    vec3 intensity;
};

struct PBRPointLight
{
	vec3 position;
	float radius;
	vec3 intensity;
	int numShadowSamples;

};

struct PBRSpotLight
{
	vec3 position;
	float radius;
	vec3 intensity;
	float cutoff;
	vec3 direction;
	int numShadowSamples;
    float outerCutoff;
};

layout(set = 1, binding = 0) readonly buffer dirLightBuffer
{
    PBRDirectionalLight dirLights[];
};

layout(set = 1, binding = 1) readonly buffer pointLightBuffer
{
    PBRPointLight pointLights[];
};

layout(set = 1, binding = 2) readonly buffer spotLightBuffer
{
    PBRSpotLight spotLights[];
};


// Trowbridge-Reitz GGX
// statistically approximates the ratio of microfacets aligned
// to the halway vector H
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / denom;
}

// Schlick geometry function
// statistically approximates the ratio of microfacets that overshadow each other
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

// Smiths Geometry method
// takes into account:
//      view direction (geometry obstruction)
//      light direction (geometry shadowing)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Schlick approximation of the fresnel term
// calculates the ratio between reflection and refraction of a surface
// based on the viewing angle cosTheta. F0 is the "base reflectivity"
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
} 

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}   