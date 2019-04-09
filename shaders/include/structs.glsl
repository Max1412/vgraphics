struct RTperFrameInfo
{
    int frameSampleCount;
    float RTAORadius;
    int RTAOSampleCount;
};

struct RTperFrameInfo2
{
    vec3 cameraPosWorld;
    int frameSampleCount;
    float RTAORadius;
    int RTAOSampleCount;
    int RTReflectionSampleCount;
    int RTUseLowResReflections;
};

struct MaterialInfoPBR
{
	vec3 baseColor;
	float roughness;
	vec3 f0;
	float metalness;
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

struct OffsetInfo
{
    int m_vbOffset;
    int m_ibOffset;
    int m_diffTextureID;
    int m_specTextureID;
};

struct VertexInfo
{
    vec3 pos;
    vec2 uv;
    vec3 normal;
};