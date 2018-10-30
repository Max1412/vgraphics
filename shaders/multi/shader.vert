#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(std430, set = 0, binding = 0) readonly buffer modelMatrixSSBO
{
    mat4 model[];
} mms;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out int drawID;
layout(location = 2) out vec3 passNormal;


layout (push_constant) uniform perFramePush
{
    mat4 view;
    mat4 proj;
} matrices;

void main()
{
    gl_Position = matrices.proj * matrices.view * mms.model[gl_DrawID] * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    drawID = gl_DrawID;
    passNormal = inNormal;
}