#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in int drawID;
layout(location = 2) in vec3 passNormal;
layout(location = 3) in vec3 passWorldPos;

layout(location = 0) out vec4 gbufferPosition;
layout(location = 1) out vec4 gbufferNormal;
layout(location = 2) out vec2 gbufferUV;


void main()
{
    gbufferPosition = vec4(passWorldPos, drawID);
    gbufferNormal = vec4(passNormal, 0.0f);
    gbufferUV = fragTexCoord;
}